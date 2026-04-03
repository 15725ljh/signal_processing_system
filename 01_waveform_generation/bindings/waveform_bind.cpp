/*
 * GUI pybind11 绑定 — 薄适配层
 *
 * 不再重写信号生成逻辑,直接调用共享库 waveform_core 的 generate_waveform()
 * 仅负责: py::dict → WaveformParams → generate_waveform() → WaveformResult → py::dict
 */
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <complex>
#include <string>
#include <cmath>

// 共享库头文件
#include "waveform_core.h"

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

// ── WaveformResult → numpy dict ──
static py::dict result_to_dict(const WaveformResult& r) {
    py::dict out;

    // radar_sig → complex128 numpy array (nrn, nan1)
    auto sig = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf = sig.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf(i, k) = r.radar_sig(i, k);
    out["radar_sig"] = sig;

    // f → float64
    if (r.has_f) {
        auto arr_f = py::array_t<double>(r.nan1);
        auto buf_f = arr_f.mutable_unchecked<1>();
        for (int k = 0; k < r.nan1; ++k) buf_f(k) = r.f(k);
        out["f"] = arr_f;
    }
    out["has_freq_hop"] = r.has_f;

    // phi1 → complex128
    if (r.has_phi1) {
        auto arr_phi = py::array_t<std::complex<double>>(r.nan1);
        auto buf_phi = arr_phi.mutable_unchecked<1>();
        for (int k = 0; k < r.nan1; ++k) buf_phi(k) = r.phi1(k);
        out["phi1"] = arr_phi;
    }
    out["has_random_phase"] = r.has_phi1;

    // freq_seq → float64
    if (r.has_freq_seq) {
        auto arr_fs = py::array_t<double>(r.nan1);
        auto buf_fs = arr_fs.mutable_unchecked<1>();
        for (int k = 0; k < r.nan1; ++k) buf_fs(k) = r.freq_seq(k);
        out["freq_seq"] = arr_fs;
    }
    out["has_freq_seq"] = r.has_freq_seq;

    out["nrn"] = r.nrn;
    out["nan1"] = r.nan1;
    out["log_output"] = r.log;

    return out;
}

// ── 主入口: py::dict → WaveformParams → generate_waveform() → py::dict ──
static py::dict run_waveform(int mode, const py::dict& system_cfg, const py::dict& waveform_cfg) {
    // 系统参数
    WaveformParams p;
    p.fc   = get_d(system_cfg, "fc",   16e9);
    p.Tp   = get_d(system_cfg, "Tp",   12e-6);
    p.B    = get_d(system_cfg, "B",    40e6);
    p.prf  = get_d(system_cfg, "prf",  10e3);
    p.Vr   = get_d(system_cfg, "Vr",   50.0);
    p.Rs   = get_d(system_cfg, "Rs",   10000.0);
    p.wr   = get_d(system_cfg, "wr",   608.0);
    p.nan1 = get_i(system_cfg, "nan1", 64);

    // Case-specific
    p.case1_N       = get_i(waveform_cfg, "case1_freq_hop.N", 10);
    p.case1_delta_f = get_d(waveform_cfg, "case1_freq_hop.delta_f", p.B);

    p.case3_prt       = get_d(waveform_cfg, "case3_pri_jitter.prt", 1000e-6);
    p.case3_amp       = get_d(waveform_cfg, "case3_pri_jitter.amp", 1.0);
    p.case3_jitter_us = get_i(waveform_cfg, "case3_pri_jitter.jitter_us", 20);

    p.case4_delta_f   = get_d(waveform_cfg, "case4_hybrid.delta_f", p.B);
    p.case4_fcnum     = get_i(waveform_cfg, "case4_hybrid.fcnum", 16);
    p.case4_amp       = get_d(waveform_cfg, "case4_hybrid.amp", 1.0);
    p.case4_prt       = get_d(waveform_cfg, "case4_hybrid.prt", 1000e-6);
    p.case4_jitter_us = get_i(waveform_cfg, "case4_hybrid.jitter_us", 20);

    p.case5_N       = get_i(waveform_cfg, "case5_combined.N", 10);
    p.case5_delta_f = get_d(waveform_cfg, "case5_combined.delta_f", p.B);

    // 调用共享库
    WaveformResult result = generate_waveform(mode, p);

    return result_to_dict(result);
}

PYBIND11_MODULE(waveform_cpp, m) {
    m.doc() = "Radar waveform generation module (C++ backend via shared waveform_core library)";
    m.def("run_waveform", &run_waveform,
          "Run waveform generation for a given mode (1-5)",
          py::arg("mode"), py::arg("system_cfg"), py::arg("waveform_cfg"));
}
