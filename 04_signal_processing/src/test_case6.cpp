/*
 * test_case6.cpp — 独立测试 Module04 Case6 (时频干扰解耦) 的抑制效果
 *
 * 对比基准: Module03 的 JSR 结果 (ISDJ~23dB, RDJ~22dB, NBJ~9.5dB)
 *
 * 测试方案:
 *   1. 用 Module03 相同的参数生成含干扰回波(EchoGenerator)
 *   2. 对所有脉冲执行 jamTarDivi (Case6 的做法)
 *   3. 计算全局 JSR, 与 Module03 基准对比
 *   4. 验证能量守恒: |target| + |jam| ≈ |input|
 */
#include <iostream>
#include <iomanip>
#include <complex>
#include <cmath>
#include <random>
#include <Eigen/Dense>
#include "Config.h"
#include "JamTarDivi.h"
#include "TfrStft.h"

#define CFG Config::instance()

using namespace Eigen;
using namespace std;

// ── 生成含干扰的仿真回波 (复用 Module03 的 EchoGenerator 参数体系) ──
// 这里用简化版: 生成LFM目标信号 + 可选干扰(ISRJ/RDJ/NBJ)
const double c = 3e8;
const double pi = M_PI;

// 精确标量相位
inline complex<double> precise_expj(double x) {
    double frac = x - floor(x);
    double phase = 2.0 * pi * frac;
    return complex<double>(cos(phase), sin(phase));
}

// 生成单个脉冲的 LFM 目标回波
VectorXcd gen_lfm_target(int nrn, double fs, double Tstart, double fc,
                          double gama, double Tp, double R, double amp) {
    VectorXcd sig = VectorXcd::Zero(nrn);
    for (int i = 0; i < nrn; ++i) {
        double t = Tstart + i / fs;
        double tau = t - 2.0 * R / c;
        if (tau >= -Tp / 2.0 && tau <= Tp / 2.0) {
            // LFM相位
            complex<double> ph1 = exp(complex<double>(0, pi * gama * tau * tau));
            // 精确载波相位 (数字载波)
            double carrier_phase = 2.0 * pi * (fc / fs) * i;
            complex<double> base = precise_expj(fc * Tstart);
            complex<double> carrier = exp(complex<double>(0, carrier_phase)) * base;
            sig(i) = amp * ph1 * carrier;
        }
    }
    return sig;
}

// 添加窄带噪声干扰 (Case4 类型)
VectorXcd add_nbj(const VectorXcd& sig, double fs, double fc, double power_dBW) {
    static default_random_engine gen(42);
    double power_linear = pow(10.0, power_dBW / 10.0);
    double sigma = sqrt(power_linear / 2.0);
    normal_distribution<double> dist(0.0, 1.0);

    VectorXcd jam = VectorXcd::Zero(sig.size());
    for (int i = 0; i < sig.size(); ++i) {
        // 窄带: 只在部分频带加噪声
        double real_part = dist(gen) * sigma;
        double imag_part = dist(gen) * sigma;
        // 载波调制 (用数字载波)
        double carrier_phase = 2.0 * pi * (fc / fs) * i;
        complex<double> carrier = exp(complex<double>(0, carrier_phase));
        jam(i) = complex<double>(real_part, imag_part) * carrier;
    }
    return sig + jam;
}

// 添加距离假目标干扰 (Case1 类型)
MatrixXcd add_rdj(const MatrixXcd& Radar_Sig, double fs, int nrn, int nan1,
                   double Rj, double amp_j) {
    double t0 = 2 * Rj / c;
    MatrixXcd result = Radar_Sig;
    VectorXd fr_local(nrn);
    for (int i = 0; i < nrn; ++i) {
        fr_local(i) = (i - nrn / 2.0) / nrn * fs;
    }

    for (int k = 1; k < nan1; ++k) {
        VectorXcd sig = Radar_Sig.col(k - 1);
        // FFT
        VectorXcd sig_fft(nrn);
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * nrn);
        fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * nrn);
        fftw_plan p = fftw_plan_dft_1d(nrn, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
        for (int i = 0; i < nrn; ++i) { in[i][0] = sig(i).real(); in[i][1] = sig(i).imag(); }
        fftw_execute(p);
        for (int i = 0; i < nrn; ++i) {
            complex<double> phase = exp(complex<double>(0, -2.0 * pi * fr_local(i) * t0));
            complex<double> val(out[i][0], out[i][1]);
            val *= phase;
            out[i][0] = val.real(); out[i][1] = val.imag();
        }
        // IFFT
        fftw_plan p2 = fftw_plan_dft_1d(nrn, out, in, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(p2);
        for (int i = 0; i < nrn; ++i) {
            result(i, k) += amp_j * complex<double>(in[i][0], in[i][1]) / double(nrn);
        }
        fftw_destroy_plan(p);
        fftw_destroy_plan(p2);
        fftw_free(in);
        fftw_free(out);
    }
    return result;
}

// 添加间歇采样转发干扰 (ISRJ, Case3 类型)
MatrixXcd add_isrj(const MatrixXcd& Radar_Sig, int nrn, int nan1,
                    double fs, double Ts_ISRJ, double T_ISRJ, double Rj, double amp_j) {
    MatrixXcd result = Radar_Sig;
    int Ts_Num = static_cast<int>(Ts_ISRJ * fs);
    int T_Num = static_cast<int>(T_ISRJ * fs);
    int N_ISRJ = static_cast<int>((12e-6) / Ts_ISRJ);  // Tp=12us
    int Num_C_I = static_cast<int>(Ts_ISRJ / T_ISRJ) - 1;
    double t0 = 2 * Rj / c;

    VectorXd fr_local(nrn);
    for (int i = 0; i < nrn; ++i) fr_local(i) = (i - nrn / 2.0) / nrn * fs;

    for (int m = 0; m < nan1; ++m) {
        VectorXd win_local = VectorXd::Zero(nrn);
        int start_index = -1;
        for (int i = 0; i < nrn; ++i) {
            if (abs(Radar_Sig(i, m)) > 1e-12) { start_index = i; break; }
        }
        if (start_index == -1) continue;

        for (int Ni = 0; Ni < N_ISRJ; ++Ni) {
            int idx_start = start_index + Ni * Ts_Num;
            int idx_end = start_index + T_Num + Ni * Ts_Num;
            for (int idx = idx_start; idx < idx_end && idx < nrn; ++idx)
                win_local(idx) = 1.0;
        }

        VectorXcd sig(nrn);
        for (int i = 0; i < nrn; ++i) sig(i) = Radar_Sig(i, m) * win_local(i);

        VectorXcd isrj_acc = VectorXcd::Zero(nrn);
        for (int Ni = 0; Ni < Num_C_I; ++Ni) {
            VectorXcd sig0(nrn);
            fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * nrn);
            fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * nrn);
            fftw_plan p = fftw_plan_dft_1d(nrn, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
            for (int i = 0; i < nrn; ++i) { in[i][0] = sig(i).real(); in[i][1] = sig(i).imag(); }
            fftw_execute(p);
            double doppler_delay = (Ni + 1) * T_ISRJ;
            for (int i = 0; i < nrn; ++i) {
                complex<double> phase = exp(complex<double>(0, -2.0 * pi * fr_local(i) * doppler_delay));
                complex<double> val(out[i][0], out[i][1]);
                val *= phase;
                out[i][0] = val.real(); out[i][1] = val.imag();
            }
            fftw_plan p2 = fftw_plan_dft_1d(nrn, out, in, FFTW_BACKWARD, FFTW_ESTIMATE);
            fftw_execute(p2);
            for (int i = 0; i < nrn; ++i) isrj_acc(i) += complex<double>(in[i][0], in[i][1]) / double(nrn);
            fftw_destroy_plan(p); fftw_destroy_plan(p2); fftw_free(in); fftw_free(out);
        }

        // 距离时延
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * nrn);
        fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * nrn);
        fftw_plan p = fftw_plan_dft_1d(nrn, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
        for (int i = 0; i < nrn; ++i) { in[i][0] = isrj_acc(i).real(); in[i][1] = isrj_acc(i).imag(); }
        fftw_execute(p);
        for (int i = 0; i < nrn; ++i) {
            complex<double> phase = exp(complex<double>(0, -2.0 * pi * fr_local(i) * t0));
            complex<double> val(out[i][0], out[i][1]);
            val *= phase;
            out[i][0] = val.real(); out[i][1] = val.imag();
        }
        fftw_plan p2 = fftw_plan_dft_1d(nrn, out, in, FFTW_BACKWARD, FFTW_ESTIMATE);
        fftw_execute(p2);
        for (int i = 0; i < nrn; ++i) result(i, m) += amp_j * complex<double>(in[i][0], in[i][1]) / double(nrn);
        fftw_destroy_plan(p); fftw_destroy_plan(p2); fftw_free(in); fftw_free(out);
    }
    return result;
}

int main() {
    Config::instance().load();
    cout << fixed << setprecision(2);

    // ── 使用 Module04 的参数体系 ──
    const double fc = 16e9;
    const double B = 40e6;
    const double Tp = 12e-6;
    const double fs = 3.0 * B;
    const double gama = B / Tp;
    const int nan1 = 64;
    const double Rs = 10000.0;
    const int nrn = (int)floor((Tp * fs + 608.0) / 2.0) * 2;
    const double Tstart = 2.0 * Rs / c - nrn / 2.0 / fs;
    const double prf = 10e3;
    const double Vr = 50.0;

    cout << "===== Module04 Case6 独立测试 =====" << endl;
    cout << "参数: fc=" << fc/1e9 << "GHz, B=" << B/1e6 << "MHz, Tp=" << Tp/1e-6 << "us"
         << ", nrn=" << nrn << ", nan1=" << nan1 << endl;
    cout << "基准: Module03 JSR ≈ 9~24 dB (取决于干扰类型)" << endl;
    cout << endl;

    // ── 生成目标信号矩阵 (nrn × nan1) ──
    MatrixXcd Radar_Sig = MatrixXcd::Zero(nrn, nan1);
    for (int k = 0; k < nan1; ++k) {
        double R = Rs + Vr * k / prf;
        Radar_Sig.col(k) = gen_lfm_target(nrn, fs, Tstart, fc, gama, Tp, R, 1.0);
    }
    double target_energy = Radar_Sig.cwiseAbs().matrix().norm();
    cout << "纯目标信号 Frobenius范数: " << target_energy << endl;

    // ── 测试场景 ──
    struct TestResult { string name; double jsr; double energy_ratio; int gaojiepu_count; };
    vector<TestResult> results;

    // === 测试1: ISRJ 干扰 ===
    {
        cout << "\n--- 测试1: ISRJ 间歇采样转发干扰 ---" << endl;
        MatrixXcd jammed = add_isrj(Radar_Sig, nrn, nan1, fs, 4e-6, 1e-6, 10.0, 10.0);
        double input_energy = jammed.cwiseAbs().matrix().norm();

        MatrixXcd jam_sig = MatrixXcd::Zero(nrn, nan1);
        MatrixXcd tar_sig = MatrixXcd::Zero(nrn, nan1);
        int gaojiepu_count = 0;

        for (int i = 0; i < nan1; ++i) {
            JamTarDiviResult r = jamTarDivi(jammed.col(i));
            jam_sig.col(i) = r.jammingsignal;
            tar_sig.col(i) = r.targetsignal;
            if (r.gaojiepu_idx == 1) gaojiepu_count++;
        }

        double jam_energy = jam_sig.cwiseAbs().matrix().norm();
        double sep_target_energy = tar_sig.cwiseAbs().matrix().norm();

        // JSR: 对比 Module03 的计算方式
        // kkk = |s_echo_noise|_max / |Echo|_max  (干信比基准)
        // kkk1 = |target|_max / |jam|_max  (分离后)
        // JSR = 20*log10(kkk1/kkk)
        double kkk = 0, kkk1 = 0;
        for (int i = 0; i < nan1; ++i) {
            double jam_max = (jammed.col(i).cwiseAbs().maxCoeff());
            double target_max = (Radar_Sig.col(i).cwiseAbs().maxCoeff());
            if (target_max > 0) kkk = max(kkk, jam_max / target_max);

            double sep_t = tar_sig.col(i).cwiseAbs().maxCoeff();
            double sep_j = jam_sig.col(i).cwiseAbs().maxCoeff();
            if (sep_j > 0) kkk1 = max(kkk1, sep_t / sep_j);
        }
        double jsr = 20.0 * log10(kkk1 / max(kkk, 1e-12));
        double energy_ratio = (jam_energy + sep_target_energy) / input_energy;

        cout << "  输入范数=" << input_energy << " 干扰分离=" << jam_energy
             << " 目标分离=" << sep_target_energy << endl;
        cout << "  JSR干扰抑制比=" << jsr << " dB  能量守恒比=" << energy_ratio
             << "  高阶谱脉冲=" << gaojiepu_count << "/" << nan1 << endl;
        results.push_back({"ISRJ", jsr, energy_ratio, gaojiepu_count});
    }

    // === 测试2: RDJ 距离假目标干扰 ===
    {
        cout << "\n--- 测试2: RDJ 距离假目标干扰 ---" << endl;
        MatrixXcd jammed = add_rdj(Radar_Sig, fs, nrn, nan1, 100.0, 10.0);
        double input_energy = jammed.cwiseAbs().matrix().norm();

        MatrixXcd jam_sig = MatrixXcd::Zero(nrn, nan1);
        MatrixXcd tar_sig = MatrixXcd::Zero(nrn, nan1);
        int gaojiepu_count = 0;

        for (int i = 0; i < nan1; ++i) {
            JamTarDiviResult r = jamTarDivi(jammed.col(i));
            jam_sig.col(i) = r.jammingsignal;
            tar_sig.col(i) = r.targetsignal;
            if (r.gaojiepu_idx == 1) gaojiepu_count++;
        }

        double jam_energy = jam_sig.cwiseAbs().matrix().norm();
        double sep_target_energy = tar_sig.cwiseAbs().matrix().norm();

        double kkk = 0, kkk1 = 0;
        for (int i = 0; i < nan1; ++i) {
            double jam_max = (jammed.col(i).cwiseAbs().maxCoeff());
            double target_max = (Radar_Sig.col(i).cwiseAbs().maxCoeff());
            if (target_max > 0) kkk = max(kkk, jam_max / target_max);
            double sep_t = tar_sig.col(i).cwiseAbs().maxCoeff();
            double sep_j = jam_sig.col(i).cwiseAbs().maxCoeff();
            if (sep_j > 0) kkk1 = max(kkk1, sep_t / sep_j);
        }
        double jsr = 20.0 * log10(kkk1 / max(kkk, 1e-12));
        double energy_ratio = (jam_energy + sep_target_energy) / input_energy;

        cout << "  输入范数=" << input_energy << " 干扰分离=" << jam_energy
             << " 目标分离=" << sep_target_energy << endl;
        cout << "  JSR干扰抑制比=" << jsr << " dB  能量守恒比=" << energy_ratio
             << "  高阶谱脉冲=" << gaojiepu_count << "/" << nan1 << endl;
        results.push_back({"RDJ", jsr, energy_ratio, gaojiepu_count});
    }

    // === 测试3: NBJ 窄带噪声干扰 ===
    {
        cout << "\n--- 测试3: NBJ 窄带噪声干扰 ---" << endl;
        MatrixXcd jammed(nrn, nan1);
        for (int k = 0; k < nan1; ++k) {
            jammed.col(k) = add_nbj(Radar_Sig.col(k), fs, fc, 20.0);
        }
        double input_energy = jammed.cwiseAbs().matrix().norm();

        MatrixXcd jam_sig = MatrixXcd::Zero(nrn, nan1);
        MatrixXcd tar_sig = MatrixXcd::Zero(nrn, nan1);
        int gaojiepu_count = 0;

        for (int i = 0; i < nan1; ++i) {
            JamTarDiviResult r = jamTarDivi(jammed.col(i));
            jam_sig.col(i) = r.jammingsignal;
            tar_sig.col(i) = r.targetsignal;
            if (r.gaojiepu_idx == 1) gaojiepu_count++;
        }

        double jam_energy = jam_sig.cwiseAbs().matrix().norm();
        double sep_target_energy = tar_sig.cwiseAbs().matrix().norm();

        double kkk = 0, kkk1 = 0;
        for (int i = 0; i < nan1; ++i) {
            double jam_max = (jammed.col(i).cwiseAbs().maxCoeff());
            double target_max = (Radar_Sig.col(i).cwiseAbs().maxCoeff());
            if (target_max > 0) kkk = max(kkk, jam_max / target_max);
            double sep_t = tar_sig.col(i).cwiseAbs().maxCoeff();
            double sep_j = jam_sig.col(i).cwiseAbs().maxCoeff();
            if (sep_j > 0) kkk1 = max(kkk1, sep_t / sep_j);
        }
        double jsr = 20.0 * log10(kkk1 / max(kkk, 1e-12));
        double energy_ratio = (jam_energy + sep_target_energy) / input_energy;

        cout << "  输入范数=" << input_energy << " 干扰分离=" << jam_energy
             << " 目标分离=" << sep_target_energy << endl;
        cout << "  JSR干扰抑制比=" << jsr << " dB  能量守恒比=" << energy_ratio
             << "  高阶谱脉冲=" << gaojiepu_count << "/" << nan1 << endl;
        results.push_back({"NBJ", jsr, energy_ratio, gaojiepu_count});
    }

    // === 测试4: 无干扰纯目标 (基准) ===
    {
        cout << "\n--- 测试4: 无干扰纯目标 (基准) ---" << endl;
        MatrixXcd jam_sig = MatrixXcd::Zero(nrn, nan1);
        MatrixXcd tar_sig = MatrixXcd::Zero(nrn, nan1);
        int gaojiepu_count = 0;

        for (int i = 0; i < nan1; ++i) {
            JamTarDiviResult r = jamTarDivi(Radar_Sig.col(i));
            jam_sig.col(i) = r.jammingsignal;
            tar_sig.col(i) = r.targetsignal;
            if (r.gaojiepu_idx == 1) gaojiepu_count++;
        }

        double jam_energy = jam_sig.cwiseAbs().matrix().norm();
        double tar_energy = tar_sig.cwiseAbs().matrix().norm();
        double ratio = (jam_energy + tar_energy) / target_energy;

        cout << "  目标范数=" << target_energy << " 误判干扰=" << jam_energy
             << " 保留目标=" << tar_energy << endl;
        cout << "  误判干扰占比=" << (jam_energy / target_energy * 100) << "%"
             << "  高阶谱脉冲=" << gaojiepu_count << "/" << nan1 << endl;
        results.push_back({"无干扰", 0.0, ratio, gaojiepu_count});
    }

    // ── 汇总对比 ──
    cout << "\n===== 汇总: Module04 Case6 vs Module03 基准 =====" << endl;
    cout << "干扰类型 | Module04 JSR干扰抑制比(dB) | Module03基准(dB) | 能量守恒比 | 高阶谱脉冲" << endl;
    cout << "---------|-----------------|----------------|-----------|----------" << endl;
    cout << "ISRJ     | " << setw(15) << results[0].jsr << " | ~21-24         | " << setw(9) << results[0].energy_ratio
         << " | " << results[0].gaojiepu_count << "/" << nan1 << endl;
    cout << "RDJ      | " << setw(15) << results[1].jsr << " | ~22            | " << setw(9) << results[1].energy_ratio
         << " | " << results[1].gaojiepu_count << "/" << nan1 << endl;
    cout << "NBJ      | " << setw(15) << results[2].jsr << " | ~9.5           | " << setw(9) << results[2].energy_ratio
         << " | " << results[2].gaojiepu_count << "/" << nan1 << endl;
    cout << "无干扰   | " << setw(15) << "N/A" << " | N/A            | " << setw(9) << results[3].energy_ratio
         << " | " << results[3].gaojiepu_count << "/" << nan1 << endl;

    return 0;
}
