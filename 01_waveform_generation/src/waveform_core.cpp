#include "waveform_core.h"
#include <cmath>
#include <random>
#include <numeric>
#include <algorithm>
#include <sstream>

using namespace Eigen;
using namespace std;

// ── 常量 ──
static constexpr double PI_d = 3.14159265358979323846;
static constexpr double c_light = 3e8;
static constexpr std::complex<double> I_c(0, 1);

// ── 高精度辅助函数(与 Module1.h 算法一致) ──
static inline std::complex<double> precise_expj_2pi(double x) {
    double frac = x - std::floor(x);
    double phase = 2.0 * PI_d * frac;
    return std::complex<double>(std::cos(phase), std::sin(phase));
}

static inline ArrayXcd precise_carrier(double freq, int N, double fs, double Tstart) {
    ArrayXd n = ArrayXd::LinSpaced(N, 0.0, N - 1.0);
    ArrayXd carrier_phase = 2.0 * PI_d * (freq / fs) * n;
    auto base = precise_expj_2pi(freq * Tstart);
    return (carrier_phase * I_c).exp() * base;
}

// ── 派生参数 ──
struct DerivedParams {
    int nrn;
    double fs, gama, Tstart;
    VectorXd tnrn;
};

static DerivedParams compute_derived(const WaveformParams& p) {
    DerivedParams d;
    d.fs = 3.0 * p.B;
    d.gama = p.B / p.Tp;
    d.nrn = (int)std::floor((p.Tp * d.fs + p.wr) / 2.0) * 2;
    double Tnrn = 1.0 / d.fs;
    d.Tstart = 2.0 * p.Rs / c_light - d.nrn / 2.0 / d.fs;
    d.tnrn.resize(d.nrn);
    for (int i = 0; i < d.nrn; ++i)
        d.tnrn(i) = d.Tstart + i * Tnrn;
    return d;
}

// ═══════════════════════════════════════════════════════════════
// Case 1: 固定跳频波形
// ═══════════════════════════════════════════════════════════════
static void case1(const WaveformParams& p, const DerivedParams& d,
                  WaveformResult& r, std::ostringstream& oss) {
    oss << "跳频点数: " << p.case1_N << "\n跳频点间隔: " << p.case1_delta_f << " Hz\n";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, p.case1_N);

    for (int k = 0; k < p.nan1; ++k) {
        int gg = dis(gen);
        r.f(k) = p.fc + gg * p.case1_delta_f;

        double R = p.Rs + p.Vr * k / p.prf;
        ArrayXd tau = d.tnrn.array() - 2 * R / c_light;
        ArrayXd win = (tau >= -p.Tp / 2).cast<double>() * (tau <= p.Tp / 2).cast<double>();

        ArrayXcd phase1 = (PI_d * d.gama * tau.square()).cast<std::complex<double>>();
        auto ph_const = precise_expj_2pi(-2.0 * r.f(k) * R / c_light);
        ArrayXcd carrier = precise_carrier(r.f(k), d.nrn, d.fs, d.Tstart);
        r.radar_sig.col(k) = win * (phase1 * I_c).exp() * ph_const * carrier;
    }
    r.has_f = true;
}

// ═══════════════════════════════════════════════════════════════
// Case 2: 随机相位波形
// ═══════════════════════════════════════════════════════════════
static void case2(const WaveformParams& p, const DerivedParams& d,
                  WaveformResult& r, std::ostringstream& oss) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, 1);

    for (int i = 0; i < p.nan1; ++i) {
        double randomPhase = 2 * PI_d * dis(gen);
        r.phi1(i) = std::exp(I_c * randomPhase);
    }

    for (int i = 0; i < p.nan1; ++i) {
        double tm = static_cast<double>(i) / p.prf;
        double R = p.Rs + p.Vr * tm;

        ArrayXd tau = d.tnrn.array() - 2 * R / c_light;
        ArrayXd win = (tau >= -p.Tp / 2).cast<double>() * (tau <= p.Tp / 2).cast<double>();

        ArrayXcd phase1 = (PI_d * d.gama * tau.square()).cast<std::complex<double>>();
        auto ph_const = precise_expj_2pi(-2.0 * p.fc * R / c_light);
        ArrayXcd carrier = precise_carrier(p.fc, d.nrn, d.fs, d.Tstart);
        r.radar_sig.col(i) = win * (phase1 * I_c).exp() * ph_const * carrier * r.phi1(i);
    }
    r.has_phi1 = true;
}

// ═══════════════════════════════════════════════════════════════
// Case 3: 脉冲重复间隔抖动波形
// ═══════════════════════════════════════════════════════════════
static void case3(const WaveformParams& p, const DerivedParams& d,
                  WaveformResult& r, std::ostringstream& oss) {
    oss << "信号幅度: " << p.case3_amp << "\n脉冲重复间隔: " << p.case3_prt / 1e-6
        << " μs\n抖动范围: ±" << p.case3_jitter_us << " μs\n";

    VectorXd tm(p.nan1);
    for (int i = 0; i < p.nan1; ++i) tm(i) = i * p.case3_prt;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(-p.case3_jitter_us, p.case3_jitter_us);

    VectorXd prt0(p.nan1);
    for (int i = 0; i < p.nan1; ++i)
        prt0(i) = p.case3_prt + dis(gen) * 1e-6;

    VectorXd tnan(p.nan1);
    tnan(0) = prt0(0);
    for (int i = 1; i < p.nan1; ++i)
        tnan(i) = tnan(i - 1) + prt0(i);

    VectorXd freq(p.nan1);
    freq(0) = 0;
    for (int i = 1; i < p.nan1; ++i)
        freq(i) = p.fc * tnan(i) / tm(i);

    for (int k = 0; k < p.nan1; ++k) {
        double R = p.Rs - p.Vr * tnan(k);
        ArrayXd tau = d.tnrn.array() - 2 * R / c_light;
        ArrayXd win = (tau >= -p.Tp / 2).cast<double>() * (tau <= p.Tp / 2).cast<double>();

        ArrayXcd phase1 = (PI_d * d.gama * tau.square()).cast<std::complex<double>>();
        auto ph_const = precise_expj_2pi(-2.0 * p.fc * R / c_light);
        r.radar_sig.col(k) = win * p.case3_amp * (phase1 * I_c).exp() * ph_const;
    }
    r.freq_seq = freq;
    r.has_freq_seq = true;
}

// ═══════════════════════════════════════════════════════════════
// Case 4: 混合波形 (跳频 + 脉冲抖动)
// ═══════════════════════════════════════════════════════════════
static void case4(const WaveformParams& p, const DerivedParams& d,
                  WaveformResult& r, std::ostringstream& oss) {
    int pulg_num = p.nan1 / p.case4_fcnum;
    oss << "频率步进: " << p.case4_delta_f << " Hz\n载频分组数: " << p.case4_fcnum
        << "\n每组脉冲数: " << pulg_num << "\n信号幅度: " << p.case4_amp
        << "\n脉冲重复间隔: " << p.case4_prt / 1e-6 << " μs\n抖动范围: ±"
        << p.case4_jitter_us << " μs\n";

    VectorXd aa(p.nan1);
    std::random_device rd;
    std::mt19937 gen(rd());
    for (int i = 0; i < pulg_num; ++i) {
        std::vector<int> perm(p.case4_fcnum);
        std::iota(perm.begin(), perm.end(), 1);
        std::shuffle(perm.begin(), perm.end(), gen);
        for (int j = 0; j < p.case4_fcnum; ++j)
            aa(i * p.case4_fcnum + j) = perm[j] - (int)std::floor(p.case4_fcnum / 2);
    }

    VectorXd freq = (aa.array() * p.case4_delta_f + p.fc).matrix();

    VectorXd tm(p.nan1);
    for (int i = 0; i < p.nan1; ++i) tm(i) = i * p.case4_prt;

    std::uniform_int_distribution<> dis(-p.case4_jitter_us, p.case4_jitter_us);
    VectorXd prt0(p.nan1);
    for (int i = 0; i < p.nan1; ++i)
        prt0(i) = p.case4_prt + dis(gen) * 1e-6;

    VectorXd tnan(p.nan1);
    tnan(0) = prt0(0);
    for (int i = 1; i < p.nan1; ++i)
        tnan(i) = tnan(i - 1) + prt0(i);

    VectorXd freq0(p.nan1);
    freq0(0) = 0;
    for (int i = 1; i < p.nan1; ++i)
        freq0(i) = freq(i) * tnan(i) / tm(i);

    for (int k = 0; k < p.nan1; ++k) {
        double R = p.Rs - p.Vr * tnan(k);
        ArrayXd tau = d.tnrn.array() - 2 * R / c_light;
        ArrayXd win = (tau >= -p.Tp / 2).cast<double>() * (tau <= p.Tp / 2).cast<double>();

        ArrayXcd phase1 = (PI_d * d.gama * tau.square()).cast<std::complex<double>>();
        auto ph_const = precise_expj_2pi(-2.0 * freq(k) * R / c_light);
        r.radar_sig.col(k) = win * p.case4_amp * (phase1 * I_c).exp() * ph_const;
    }
    r.freq_seq = freq0;
    r.has_freq_seq = true;
}

// ═══════════════════════════════════════════════════════════════
// Case 5: 跳频 + 随机相位复合波形
// ═══════════════════════════════════════════════════════════════
static void case5(const WaveformParams& p, const DerivedParams& d,
                  WaveformResult& r, std::ostringstream& oss) {
    oss << "跳频点数: " << p.case5_N << "\n频率步进: " << p.case5_delta_f << " Hz\n";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis_gg(1, p.case5_N);
    std::uniform_real_distribution<> dis_phi(0, 1);

    for (int i = 0; i < p.nan1; ++i) {
        double randomPhase = 2 * PI_d * dis_phi(gen);
        r.phi1(i) = std::exp(std::complex<double>(0, randomPhase));
    }

    for (int k = 0; k < p.nan1; ++k) {
        int gg = dis_gg(gen);
        r.f(k) = p.fc + gg * p.case5_delta_f;

        double R = p.Rs + p.Vr * k / p.prf;
        ArrayXd tau = d.tnrn.array() - 2 * R / c_light;
        ArrayXd win = (tau >= -p.Tp / 2).cast<double>() * (tau <= p.Tp / 2).cast<double>();

        ArrayXcd phase1 = (PI_d * d.gama * tau.square()).cast<std::complex<double>>();
        auto ph_const = precise_expj_2pi(-2.0 * r.f(k) * R / c_light);
        ArrayXcd carrier = precise_carrier(r.f(k), d.nrn, d.fs, d.Tstart);
        r.radar_sig.col(k) = win * (phase1 * I_c).exp() * ph_const * carrier * r.phi1(k);
    }
    r.has_f = true;
    r.has_phi1 = true;
}

// ═══════════════════════════════════════════════════════════════
// 主入口
// ═══════════════════════════════════════════════════════════════
WaveformResult generate_waveform(int mode, const WaveformParams& p) {
    WaveformResult r;
    r.nan1 = p.nan1;

    DerivedParams d = compute_derived(p);
    r.nrn = d.nrn;
    r.radar_sig = MatrixXcd::Zero(d.nrn, p.nan1);
    r.f = VectorXd::Zero(p.nan1);
    r.phi1 = VectorXcd::Zero(p.nan1);
    r.freq_seq = VectorXd::Zero(p.nan1);

    std::ostringstream oss;
    oss << "载波频率 fc: " << p.fc << " Hz\n"
        << "脉冲宽度 Tp: " << p.Tp << " s\n"
        << "信号带宽 B: " << p.B << " Hz\n"
        << "采样频率 fs: " << d.fs << " Hz\n"
        << "线性调频率 gama: " << d.gama << " Hz/s\n"
        << "脉冲重复频率 prf: " << p.prf << " Hz\n"
        << "距离向采样点数 nrn: " << d.nrn << "\n"
        << "方位向脉冲数 nan1: " << p.nan1 << "\n";

    const char* modeNames[] = {"", "固定跳频波形", "随机相位波形",
                              "脉冲重复间隔抖动波形", "混合波形(跳频+抖动)",
                              "跳频+随机相位复合波形"};

    if (mode >= 1 && mode <= 5)
        oss << "\n模式" << mode << ": " << modeNames[mode] << "\n";

    switch (mode) {
        case 1: case1(p, d, r, oss); break;
        case 2: case2(p, d, r, oss); break;
        case 3: case3(p, d, r, oss); break;
        case 4: case4(p, d, r, oss); break;
        case 5: case5(p, d, r, oss); break;
        default: break;
    }

    r.log = oss.str();
    return r;
}
