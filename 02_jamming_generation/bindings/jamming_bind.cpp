/*
 * GUI pybind11 绑定 — 干扰生成模块
 *
 * 直接调用 jamming_core 的 generate_jamming()
 * 负责参数转换: py::dict → JammingParams → generate_jamming() → JammingResult → py::dict
 */
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <complex>
#include <string>
#include <cmath>

#include "jamming_core.h"

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

// ── JammingResult → numpy dict ──
static py::dict result_to_dict(const JammingResult& r) {
    py::dict out;

    // echo_target → complex128 numpy array (nrn, nan1)
    auto echo = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_echo = echo.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_echo(i, k) = r.echo_target(i, k);
    out["echo_target"] = echo;

    // jam_signal → complex128 numpy array (nrn, nan1)
    auto jam = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_jam = jam.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_jam(i, k) = r.jam_signal(i, k);
    out["jam_signal"] = jam;

    // target_signal → complex128 numpy array (nrn, nan1)
    auto tgt = py::array_t<std::complex<double>>({r.nrn, r.nan1});
    auto buf_tgt = tgt.mutable_unchecked<2>();
    for (int i = 0; i < r.nrn; ++i)
        for (int k = 0; k < r.nan1; ++k)
            buf_tgt(i, k) = r.target_signal(i, k);
    out["target_signal"] = tgt;

    out["nrn"]  = r.nrn;
    out["nan1"] = r.nan1;
    out["log_output"] = r.log;

    return out;
}

// ── 主入口 ──
static py::dict run_jamming(int mode, const py::dict& system_cfg, const py::dict& jamming_cfg) {
    JammingParams p;

    // 系统参数
    p.fc    = get_d(system_cfg, "fc",    16e9);
    p.Tp    = get_d(system_cfg, "Tp",    12e-6);
    p.B     = get_d(system_cfg, "B",     40e6);
    p.prf   = get_d(system_cfg, "prf",   10e3);
    p.Vr    = get_d(system_cfg, "Vr",    50.0);
    p.Rs    = get_d(system_cfg, "Rs",    10000.0);
    p.wr    = get_d(system_cfg, "wr",    608.0);
    p.A_RJ  = get_d(system_cfg, "A_RJ",  10.0);
    p.z_R0  = get_d(system_cfg, "z_R0",  2000.0);
    p.nan1  = get_i(system_cfg, "nan1",  64);

    // Case 1 (RDJ)
    p.case1_jj    = get_i(jamming_cfg, "case1_rdj.jj",      1);
    p.case1_Rj    = get_d(jamming_cfg, "case1_rdj.Rj",      100.0);
    p.case1_amp_j = get_d(jamming_cfg, "case1_rdj.amp_j",   10.0);

    // Case 2 (VDJ)
    p.case2_Vj    = get_d(jamming_cfg, "case2_vdj.Vj",      1e5);
    p.case2_jj    = get_i(jamming_cfg, "case2_vdj.jj",      1);
    p.case2_Rj    = get_d(jamming_cfg, "case2_vdj.Rj",      10.0);
    p.case2_amp_j = get_d(jamming_cfg, "case2_vdj.amp_j",   10.0);

    // Case 3 (ISRJ)
    p.case3_Ts_ISRJ = get_d(jamming_cfg, "case3_isrj.Ts_ISRJ", 4e-6);
    p.case3_T_ISRJ  = get_d(jamming_cfg, "case3_isrj.T_ISRJ",  0.0);
    p.case3_Rj      = get_d(jamming_cfg, "case3_isrj.Rj",      10.0);
    p.case3_amp_j   = get_d(jamming_cfg, "case3_isrj.amp_j",   10.0);

    // Case 4 (NNJ)
    p.case4_power_dBW     = get_d(jamming_cfg, "case4_nnj.power_dBW",     20.0);
    p.case4_butter_order  = get_i(jamming_cfg, "case4_nnj.butter_order",  8);
    p.case4_butter_cutoff = get_d(jamming_cfg, "case4_nnj.butter_cutoff", 0.3);

    // Case 5 (RGPO)
    p.case5_Vj          = get_d(jamming_cfg, "case5_rgpo.Vj",          340.0);
    p.case5_amp_target  = get_d(jamming_cfg, "case5_rgpo.amp_target",  1.0);
    p.case5_amp_jammer  = get_d(jamming_cfg, "case5_rgpo.amp_jammer",  1.4);
    p.case5_drag_stages = get_i(jamming_cfg, "case5_rgpo.drag_stages", 4);
    p.case5_awgn_snr    = get_d(jamming_cfg, "case5_rgpo.awgn_snr",    10.0);

    // Case 6 (VGPO)
    p.case6_Vj          = get_d(jamming_cfg, "case6_vgpo.Vj",          1e4);
    p.case6_amp_target  = get_d(jamming_cfg, "case6_vgpo.amp_target",  1.0);
    p.case6_amp_jammer  = get_d(jamming_cfg, "case6_vgpo.amp_jammer",  1.4);
    p.case6_drag_stages = get_i(jamming_cfg, "case6_vgpo.drag_stages", 4);
    p.case6_awgn_snr    = get_d(jamming_cfg, "case6_vgpo.awgn_snr",    10.0);

    // Case 7 (DRFTJ)
    p.case7_JSR        = get_d(jamming_cfg, "case7_drftj.JSR",        0.0);
    p.case7_amp_target = get_d(jamming_cfg, "case7_drftj.amp_target", 1.0);
    p.case7_num_jam    = get_i(jamming_cfg, "case7_drftj.num_jam",    50);
    p.case7_detaR      = get_d(jamming_cfg, "case7_drftj.detaR",      50.0);
    p.case7_awgn_snr   = get_d(jamming_cfg, "case7_drftj.awgn_snr",   10.0);

    // Case 8 (IPLESRJ)
    p.case8_A_RJ         = get_d(jamming_cfg, "case8_iplesrj.A_RJ",         30.0);
    p.case8_amp_target   = get_d(jamming_cfg, "case8_iplesrj.amp_target",   1.0);
    p.case8_R0_new       = get_d(jamming_cfg, "case8_iplesrj.R0_new",       608.0);
    p.case8_V_ISRJ       = get_d(jamming_cfg, "case8_iplesrj.V_ISRJ",       0.0);
    p.case8_T_ISRJ_ratio = get_i(jamming_cfg, "case8_iplesrj.T_ISRJ_ratio", 16);
    p.case8_R_ahead      = get_d(jamming_cfg, "case8_iplesrj.R_ahead",      0.0);
    p.case8_awgn_snr     = get_d(jamming_cfg, "case8_iplesrj.awgn_snr",     10.0);

    // Case 9 (SMSP)
    p.case9_num_slices = get_i(jamming_cfg, "case9_smsp.num_slices", 4);
    p.case9_JSR        = get_d(jamming_cfg, "case9_smsp.JSR",        15.0);
    p.case9_amp_target = get_d(jamming_cfg, "case9_smsp.amp_target", 1.0);
    p.case9_amp_extra  = get_d(jamming_cfg, "case9_smsp.amp_extra",  1.4);
    p.case9_R0         = get_d(jamming_cfg, "case9_smsp.R0",         10000.0);
    p.case9_awgn_snr   = get_d(jamming_cfg, "case9_smsp.awgn_snr",   10.0);

    // Case 10 (COMB)
    p.case10_num_tones  = get_i(jamming_cfg, "case10_comb.num_tones",  7);
    p.case10_JSR        = get_d(jamming_cfg, "case10_comb.JSR",        0.0);
    p.case10_amp_target = get_d(jamming_cfg, "case10_comb.amp_target", 1.0);
    p.case10_deltaf     = get_d(jamming_cfg, "case10_comb.deltaf",     1e6);
    p.case10_R0         = get_d(jamming_cfg, "case10_comb.R0",         10000.0);
    p.case10_awgn_snr   = get_d(jamming_cfg, "case10_comb.awgn_snr",   10.0);

    JammingResult result = generate_jamming(mode, p);
    return result_to_dict(result);
}

PYBIND11_MODULE(jamming_cpp, m) {
    m.doc() = "Radar jamming generation module (C++ backend via jamming_core library)";
    m.def("run_jamming", &run_jamming,
          "Run jamming generation for a given mode (1-10)",
          py::arg("mode"), py::arg("system_cfg"), py::arg("jamming_cfg"));
}
