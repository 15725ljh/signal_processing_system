/*
 * GUI pybind11 绑定 — 干扰识别与抑制模块
 *
 * Python 端传入 detection_cfg (扁平字典) → C++ 端:
 *   1. 将字典写入临时 JSON 文件 (detection_suppression.* 节)
 *   2. 调用 run_detection(label, temp_path)
 *   3. 删除临时文件
 *   4. 转换 DetectionResult → py::dict (numpy 数组)
 */
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <complex>
#include <string>
#include <fstream>
#include <unordered_map>

#include "detection_core.h"

namespace py = pybind11;

// ── py::dict 辅助读取 ──
static double get_d(const py::dict& d, const char* key, double def_val) {
    if (d.contains(key)) {
        py::object v = d[key];
        if (py::isinstance<py::int_>(v)) return (double)v.cast<int>();
        if (py::isinstance<py::float_>(v)) return v.cast<double>();
    }
    return def_val;
}

static int get_i(const py::dict& d, const char* key, int def_val) {
    if (d.contains(key)) {
        py::object v = d[key];
        if (py::isinstance<py::int_>(v)) return v.cast<int>();
        if (py::isinstance<py::float_>(v)) return (int)v.cast<double>();
    }
    return def_val;
}

// ── 将 detection_cfg 字典写入临时 JSON 文件 ──
// 所有 key 以 "detection_suppression." 开头, 需要剥离前缀后写入嵌套结构
static std::string write_temp_config(const py::dict& detection_cfg) {
    // 创建临时文件
    std::string temp_path = std::tmpnam(nullptr);
    temp_path += "_sps_detection.json";

    // 构建嵌套 JSON: { "detection_suppression": { ... } }
    std::unordered_map<std::string, std::string> flat_params;
    for (auto item : detection_cfg) {
        std::string key = py::str(item.first).cast<std::string>();
        std::string prefix = "detection_suppression.";
        // 剥离前缀
        if (key.rfind(prefix, 0) == 0) {
            key = key.substr(prefix.size());
        }
        flat_params[key] = py::str(item.second).cast<std::string>();
    }

    std::ofstream out(temp_path);
    out << "{\n  \"detection_suppression\": {\n";

    size_t i = 0;
    for (const auto& [key, val] : flat_params) {
        out << "    \"" << key << "\": " << val;
        if (++i < flat_params.size()) out << ",";
        out << "\n";
    }

    out << "  }\n}\n";
    out.close();

    return temp_path;
}

// ── DetectionResult → numpy dict ──
static py::dict result_to_dict(const DetectionResult& r) {
    py::dict out;
    int cpiNum = r.cpiNum;
    int nrn = r.nrn;

    // echo_signal → complex128 numpy array (cpiNum, nrn)
    auto echo = py::array_t<std::complex<double>>({cpiNum, nrn});
    auto buf_echo = echo.mutable_unchecked<2>();
    for (int i = 0; i < cpiNum; ++i)
        for (int k = 0; k < nrn; ++k)
            buf_echo(i, k) = r.echo_signal(i, k);
    out["echo_signal"] = echo;

    // s_echo_noise → complex128 numpy array (nrn,)
    auto noise = py::array_t<std::complex<double>>({nrn});
    auto buf_noise = noise.mutable_unchecked<1>();
    for (int k = 0; k < nrn; ++k)
        buf_noise(k) = r.s_echo_noise(k);
    out["s_echo_noise"] = noise;

    // stft_matrix → complex128 numpy array (STFT_NUM, nrn)
    int stft_rows = r.stft_matrix.rows();
    auto stft = py::array_t<std::complex<double>>({stft_rows, nrn});
    auto buf_stft = stft.mutable_unchecked<2>();
    for (int i = 0; i < stft_rows; ++i)
        for (int k = 0; k < nrn; ++k)
            buf_stft(i, k) = r.stft_matrix(i, k);
    out["stft_matrix"] = stft;

    // jam_mask → float64 numpy array (STFT_NUM, nrn)
    auto mask = py::array_t<double>({stft_rows, nrn});
    auto buf_mask = mask.mutable_unchecked<2>();
    for (int i = 0; i < stft_rows; ++i)
        for (int k = 0; k < nrn; ++k)
            buf_mask(i, k) = r.jam_mask(i, k);
    out["jam_mask"] = mask;

    // detection_types → int32 numpy array (cpiNum,)
    auto types = py::array_t<int>({cpiNum});
    auto buf_types = types.mutable_unchecked<1>();
    for (int i = 0; i < cpiNum; ++i)
        buf_types(i) = r.detection_types(i);
    out["detection_types"] = types;

    // jamming_signal → complex128 numpy array (nrn,)
    auto jam = py::array_t<std::complex<double>>({nrn});
    auto buf_jam = jam.mutable_unchecked<1>();
    for (int k = 0; k < nrn; ++k)
        buf_jam(k) = r.jamming_signal(k);
    out["jamming_signal"] = jam;

    // target_signal → complex128 numpy array (nrn,)
    auto tgt = py::array_t<std::complex<double>>({nrn});
    auto buf_tgt = tgt.mutable_unchecked<1>();
    for (int k = 0; k < nrn; ++k)
        buf_tgt(k) = r.target_signal(k);
    out["target_signal"] = tgt;

    out["dominant_type"]  = r.dominant_type;
    out["correct_count"]  = r.correct_count;
    out["cpiNum"]         = r.cpiNum;
    out["nrn"]            = r.nrn;
    out["isr"]            = r.isr;
    out["elapsed"]        = r.elapsed;
    out["log_output"]     = r.log;

    return out;
}

// ── 主入口 ──
static py::dict run_detection_wrapper(int real_label, const py::dict& detection_cfg) {
    // 1. 写临时配置文件
    std::string temp_path = write_temp_config(detection_cfg);

    // 2. 调用检测流水线
    DetectionResult result = run_detection(real_label, temp_path);

    // 3. 删除临时文件
    std::remove(temp_path.c_str());

    // 4. 转换为 Python dict
    return result_to_dict(result);
}

PYBIND11_MODULE(detection_cpp, m) {
    m.doc() = "Radar jamming detection & suppression module (C++ backend)";
    m.def("run_detection", &run_detection_wrapper,
          "Run jamming detection, separation and ISR evaluation for a given jamming type",
          py::arg("real_label"), py::arg("detection_cfg"));
}
