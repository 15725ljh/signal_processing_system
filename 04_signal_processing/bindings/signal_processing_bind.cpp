/*
 * GUI pybind11 绑定 — 信号处理模块 (Module04)
 *
 * Python 端传入扁平字典 (system.*, recognition.*, processing.*,
 * detection_suppression.*, waveform.*) → 写入临时嵌套 JSON →
 * 调用 C++ API → 删除临时文件 → 转换为 py::dict (numpy 数组)
 */
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <complex>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>

#include "signal_processing_core.h"

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

// ── 将扁平字典写入临时嵌套 JSON 文件 (纯字符串拼接, 不依赖 nlohmann::json) ──
// key 格式: "system.fc", "waveform.case1_freq_hop.N" 等 (点分路径)
// 输出: { "system": { "fc": ... }, "waveform": { "case1_freq_hop": { "N": ... } } }
//
// 使用有序 map 按前缀分组, 递归构建嵌套 JSON 字符串
static std::string write_temp_config(const py::dict& cfg) {
    std::string temp_path = std::tmpnam(nullptr);
    temp_path += "_sps_signal_processing.json";

    // 收集所有键值对
    std::vector<std::pair<std::vector<std::string>, std::string>> entries;
    for (auto item : cfg) {
        std::string key = py::str(item.first).cast<std::string>();
        std::string val = py::str(item.second).cast<std::string>();

        // 按 '.' 拆分路径
        std::vector<std::string> parts;
        std::stringstream ss(key);
        std::string part;
        while (std::getline(ss, part, '.')) {
            if (!part.empty()) parts.push_back(part);
        }
        entries.push_back({parts, val});
    }

    // 递归序列化: 将 entries 按 parts[depth] 分组, 同一组递归处理
    // 使用尾递归展开为循环 + 显式栈
    struct Frame {
        std::map<std::string, std::vector<std::pair<std::vector<std::string>, std::string>>> groups;
        int depth;
        bool is_leaf;
        std::string result;
    };

    std::string root_result;
    // 栈: (entries, depth) → 输出 JSON 字符串
    // 用递归 lambda 实现
    std::function<std::string(
        const std::vector<std::pair<std::vector<std::string>, std::string>>&, int)> serialize;

    serialize = [&](const auto& ents, int depth) -> std::string {
        // 按 parts[depth] 分组
        std::map<std::string, std::vector<std::pair<std::vector<std::string>, std::string>>> groups;
        for (const auto& e : ents) {
            if (depth < (int)e.first.size()) {
                groups[e.first[depth]].push_back(e);
            }
        }

        std::ostringstream out;
        out << "{";
        bool first = true;
        for (const auto& [name, sub_entries] : groups) {
            if (!first) out << ",";
            out << "\n  \"" << name << "\": ";

            if (depth + 1 < (int)sub_entries[0].first.size()) {
                // 还有更深的层级, 递归
                out << serialize(sub_entries, depth + 1);
            } else {
                // 叶子节点, 直接写值
                out << sub_entries[0].second;
            }
            first = false;
        }
        if (!groups.empty()) out << "\n";
        out << "}";
        return out.str();
    };

    std::string json_str = serialize(entries, 0);

    std::ofstream out(temp_path);
    out << json_str;
    out.close();

    return temp_path;
}

// ── RecognitionResult → numpy dict ──
static py::dict recognition_to_dict(const RecognitionResult& r) {
    py::dict out;

    // stft_matrix → complex128 (STFT_NUM × nrn)
    int stft_rows = r.stft_matrix.rows();
    int stft_cols = r.stft_matrix.cols();
    auto stft = py::array_t<std::complex<double>>({stft_rows, stft_cols});
    auto buf_stft = stft.mutable_unchecked<2>();
    for (int i = 0; i < stft_rows; ++i)
        for (int k = 0; k < stft_cols; ++k)
            buf_stft(i, k) = r.stft_matrix(i, k);
    out["stft_matrix"] = stft;

    // jam_mask → float64 (STFT_NUM × nrn)
    auto mask = py::array_t<double>({stft_rows, stft_cols});
    auto buf_mask = mask.mutable_unchecked<2>();
    for (int i = 0; i < stft_rows; ++i)
        for (int k = 0; k < stft_cols; ++k)
            buf_mask(i, k) = r.jam_mask(i, k);
    out["jam_mask"] = mask;

    // echo_pulse → complex128 (nrn,)
    auto echo = py::array_t<std::complex<double>>({r.nrn});
    auto buf_echo = echo.mutable_unchecked<1>();
    for (int k = 0; k < r.nrn; ++k)
        buf_echo(k) = r.echo_pulse(k);
    out["echo_pulse"] = echo;

    out["j_type"] = r.j_type;
    out["nrn"] = r.nrn;
    out["elapsed"] = r.elapsed;
    out["log_output"] = r.log;

    return out;
}

// ── ProcessingResultRD → numpy dict ──
static py::dict processing_rd_to_dict(const ProcessingResultRD& r) {
    py::dict out;

    // rd_map → complex128 (nrn × nan1)
    auto rd = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_rd = rd.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_rd(i, k) = r.rd_map(i, k);
    out["rd_map"] = rd;

    // input_signal → complex128 (nrn × nan1)
    auto sig = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_sig = sig.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_sig(i, k) = r.input_signal(i, k);
    out["input_signal"] = sig;

    // xi → float64 (nrn,)
    auto xi = py::array_t<double>({r.nrn});
    auto buf_xi = xi.mutable_unchecked<1>();
    for (int i = 0; i < r.nrn; ++i)
        buf_xi(i) = r.xi(i);
    out["xi"] = xi;

    // dv → float64 (nan1,)
    auto dv = py::array_t<double>({r.nan1});
    auto buf_dv = dv.mutable_unchecked<1>();
    for (int k = 0; k < r.nan1; ++k)
        buf_dv(k) = r.dv(k);
    out["dv"] = dv;

    out["nrn"] = r.nrn;
    out["nan1"] = r.nan1;
    out["case_num"] = r.case_num;
    out["elapsed"] = r.elapsed;
    out["log_output"] = r.log;

    return out;
}

// ── ProcessingResultDecouple → numpy dict ──
static py::dict processing_decouple_to_dict(const ProcessingResultDecouple& r) {
    py::dict out;

    // jam_signal → complex128 (nrn × nan1)
    auto jam = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_jam = jam.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_jam(i, k) = r.jam_signal(i, k);
    out["jam_signal"] = jam;

    // target_signal → complex128 (nrn × nan1)
    auto tgt = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_tgt = tgt.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_tgt(i, k) = r.target_signal(i, k);
    out["target_signal"] = tgt;

    // input_signal → complex128 (nrn × nan1)
    auto sig = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_sig = sig.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_sig(i, k) = r.input_signal(i, k);
    out["input_signal"] = sig;

    // decouple_flag → float64 (nan1,)
    auto flag = py::array_t<double>({r.nan1});
    auto buf_flag = flag.mutable_unchecked<1>();
    for (int k = 0; k < r.nan1; ++k)
        buf_flag(k) = r.decouple_flag(k);
    out["decouple_flag"] = flag;

    out["isr_dB"] = r.isr_dB;
    out["avg_threshold"] = r.avg_threshold;
    out["gaojiepu_count"] = r.gaojiepu_count;
    out["nrn"] = r.nrn;
    out["nan1"] = r.nan1;
    out["elapsed"] = r.elapsed;
    out["log_output"] = r.log;

    return out;
}

// ── 三个入口函数 ──

static py::dict run_recognition_wrapper(int jam_type, const py::dict& config_cfg) {
    std::string temp_path = write_temp_config(config_cfg);
    RecognitionResult result = run_recognition(jam_type, temp_path);
    std::remove(temp_path.c_str());
    return recognition_to_dict(result);
}

static py::dict run_processing_rd_wrapper(int case_num, const py::dict& config_cfg) {
    std::string temp_path = write_temp_config(config_cfg);
    ProcessingResultRD result = run_processing_rd(case_num, temp_path);
    std::remove(temp_path.c_str());
    return processing_rd_to_dict(result);
}

static py::dict run_processing_decouple_wrapper(int jam_type, const py::dict& config_cfg) {
    std::string temp_path = write_temp_config(config_cfg);
    ProcessingResultDecouple result = run_processing_decouple(jam_type, temp_path);
    std::remove(temp_path.c_str());
    return processing_decouple_to_dict(result);
}

PYBIND11_MODULE(signal_processing_cpp, m) {
    m.doc() = "Radar signal processing module (C++ backend) — recognition, RD processing, decoupling";
    m.def("run_recognition", &run_recognition_wrapper,
          "Run jamming recognition (STFT + gr_detection) for a given jamming type",
          py::arg("jam_type"), py::arg("config_cfg"));
    m.def("run_processing_rd", &run_processing_rd_wrapper,
          "Run Range-Doppler processing (Cases 1-5)",
          py::arg("case_num"), py::arg("config_cfg"));
    m.def("run_processing_decouple", &run_processing_decouple_wrapper,
          "Run time-frequency jamming-target decoupling (Case 6)",
          py::arg("jam_type"), py::arg("config_cfg"));
}
