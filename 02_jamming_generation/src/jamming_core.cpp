// jamming_core.cpp — 干扰生成核心实现 (独立 API, 不依赖 Config.h / parameters.h / Module0.h)
// 所有参数通过 JammingParams 结构体传入, 工具函数内联实现

#include "jamming_core.h"
#include "jamming_params.h"

#include <Eigen/Dense>
#include <complex>
#include <cmath>
#include <vector>
#include <random>
#include <sstream>
#include <algorithm>
#include <fftw3.h>

using namespace Eigen;
using namespace std;

// ── 物理常量 ──
static constexpr double PI  = 3.14159265358979323846;
static constexpr double c   = 3e8;
static constexpr complex<double> I_complex(0, 1);

// ═══════════════════════════════════════════════════════════════════
// 内联工具函数 (从 Module0.h 复制, 仅保留 jamming 需要的部分)
// ═══════════════════════════════════════════════════════════════════

// 高精度 exp(j*2*PI*x), 对任意大小的 x 取小数部分计算, 避免大数精度丢失
static inline complex<double> precise_expj_2pi_scalar(double x) {
    double frac = x - floor(x);
    double phase = 2.0 * PI * frac;
    return complex<double>(cos(phase), sin(phase));
}

// 高精度向量 exp(j*2*PI*freq*t), 拆分为数字载波 + 起始偏移
static inline ArrayXcd precise_expj_2pi_array(double freq, const ArrayXd& t_arr,
                                               double fs_val, double Tstart_val) {
    int N = static_cast<int>(t_arr.size());
    ArrayXd n = ArrayXd::LinSpaced(N, 0.0, N - 1.0);
    ArrayXd carrier_phase = 2.0 * PI * (freq / fs_val) * n;
    complex<double> base = precise_expj_2pi_scalar(freq * Tstart_val);
    return (carrier_phase * I_complex).exp() * base;
}

// FFT (FFTW3)
static inline VectorXcd fft(const VectorXcd& input) {
    int n = static_cast<int>(input.size());
    fftw_complex *in  = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * n);
    fftw_complex *out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * n);
    for (int i = 0; i < n; ++i) {
        in[i][0] = input(i).real();
        in[i][1] = input(i).imag();
    }
    fftw_plan p = fftw_plan_dft_1d(n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(p);
    VectorXcd result(n);
    for (int i = 0; i < n; ++i)
        result(i) = complex<double>(out[i][0], out[i][1]);
    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);
    return result;
}

// IFFT (FFTW3)
static inline VectorXcd ifft(const VectorXcd& input) {
    int n = static_cast<int>(input.size());
    fftw_complex *in  = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * n);
    fftw_complex *out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * n);
    for (int i = 0; i < n; ++i) {
        in[i][0] = input(i).real();
        in[i][1] = input(i).imag();
    }
    fftw_plan p = fftw_plan_dft_1d(n, in, out, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(p);
    VectorXcd result(n);
    for (int i = 0; i < n; ++i)
        result(i) = complex<double>(out[i][0] / n, out[i][1] / n);
    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);
    return result;
}

// Butterworth 低通滤波器设计 (MATLAB butter 兼容)
static inline void butter(int order, double cutoff, vector<double>& b, vector<double>& a) {
    int n = order;
    double warped = tan(PI * cutoff);
    vector<complex<double>> analog_poles;
    for (int k = 0; k < n; ++k) {
        double theta = PI * (2.0 * k + 1.0 + n) / (2.0 * n);
        analog_poles.push_back(warped * exp(complex<double>(0, theta)));
    }
    vector<complex<double>> digital_poles;
    for (int k = 0; k < n; ++k) {
        complex<double> z = (2.0 + analog_poles[k]) / (2.0 - analog_poles[k]);
        digital_poles.push_back(z);
    }
    vector<complex<double>> a_complex = {1.0};
    for (int k = 0; k < n; ++k) {
        vector<complex<double>> new_poly(a_complex.size() + 1, 0.0);
        for (size_t i = 0; i < a_complex.size(); ++i) {
            new_poly[i] += a_complex[i];
            new_poly[i + 1] -= a_complex[i] * digital_poles[k];
        }
        a_complex = new_poly;
    }
    a.resize(a_complex.size());
    for (size_t i = 0; i < a_complex.size(); ++i)
        a[i] = a_complex[i].real();
    double a0 = a[0];
    for (size_t i = 0; i < a.size(); ++i)
        a[i] /= a0;
    b.resize(n + 1, 0.0);
    for (int i = 0; i <= n; ++i) {
        double binom = 1.0;
        for (int j = 1; j <= i; ++j)
            binom *= (n - j + 1.0) / j;
        b[i] = binom;
    }
    double sum_b = 0.0, sum_a = 0.0;
    for (size_t i = 0; i < b.size(); ++i) sum_b += b[i];
    for (size_t i = 0; i < a.size(); ++i) sum_a += a[i];
    double gain = sum_a / sum_b;
    for (size_t i = 0; i < b.size(); ++i)
        b[i] *= gain;
}

// IIR 滤波器 (MATLAB filter 兼容)
static inline VectorXcd filter(const vector<double>& b, const vector<double>& a, const VectorXcd& x) {
    int N = static_cast<int>(x.size());
    int nb = static_cast<int>(b.size());
    int na = static_cast<int>(a.size());
    VectorXcd y = VectorXcd::Zero(N);
    for (int n = 0; n < N; ++n) {
        complex<double> acc = 0.0;
        for (int i = 0; i < nb; ++i)
            if (n - i >= 0) acc += b[i] * x(n - i);
        for (int j = 1; j < na; ++j)
            if (n - j >= 0) acc -= a[j] * y(n - j);
        y(n) = acc;
    }
    return y;
}

// AWGN 加噪
static inline VectorXcd awgn(const VectorXcd& signal, double snr) {
    thread_local static mt19937 generator{random_device{}()};
    thread_local static normal_distribution<double> dist{0.0, 1.0};
    double signal_power = signal.cwiseAbs2().sum() / signal.size();
    double noise_power  = signal_power / pow(10.0, snr / 10.0);
    double noise_std    = sqrt(noise_power / 2.0);
    VectorXcd noise(signal.size());
    for (int i = 0; i < signal.size(); ++i)
        noise[i] = {dist(generator) * noise_std, dist(generator) * noise_std};
    return noise;
}

// ═══════════════════════════════════════════════════════════════════
// 局部参数计算 (替代全局变量 parameters.h)
// ═══════════════════════════════════════════════════════════════════

struct LocalParams {
    double fs_val, gama_val, lambda_val, Tstart_val, Tnrn_val;
    int nrn_val;
    VectorXd tnrn, fr;

    void compute(const JammingParams& p) {
        fs_val      = 3.0 * p.B;
        gama_val    = p.B / p.Tp;
        lambda_val  = c / p.fc;
        Tnrn_val    = 1.0 / fs_val;
        nrn_val     = static_cast<int>(floor((p.Tp * fs_val + p.wr) / 2.0)) * 2;
        Tstart_val  = 2.0 * p.Rs / c - nrn_val / 2.0 / fs_val;

        tnrn.resize(nrn_val);
        fr.resize(nrn_val);
        for (int i = 0; i < nrn_val; ++i) {
            tnrn(i) = Tstart_val + i * Tnrn_val;
            fr(i)   = (i - nrn_val / 2.0) / (nrn_val / fs_val);
        }
    }
};

// ═══════════════════════════════════════════════════════════════════
// 目标回波生成辅助函数 (Cases 1~3 共用)
// ═══════════════════════════════════════════════════════════════════

static inline MatrixXcd generate_target_echo(
    const JammingParams& p, const LocalParams& lp)
{
    MatrixXcd target_echo = MatrixXcd::Zero(lp.nrn_val, p.nan1);
    for (int k = 0; k < p.nan1; ++k) {
        double Rt = p.Rs - p.Vr * k / p.prf;
        VectorXd time_diff = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd phase = (I_complex * PI * lp.gama_val * time_diff.array().square()).exp()
                        * precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val);
        Array<bool, -1, 1> wint =
            (time_diff.array() >= -p.Tp / 2.0) && (time_diff.array() <= p.Tp / 2.0);
        target_echo.col(k) = wint.cast<double>() * phase.array();
    }
    return target_echo;
}

// ═══════════════════════════════════════════════════════════════════
// Case 1: 距离假目标干扰 (RDJ)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case1(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double t0 = 2 * p.case1_Rj / c;
    double amp_j = p.case1_amp_j;

    ostringstream oss;
    oss << "[Case1 RDJ] jj=" << p.case1_jj
        << ", Rj=" << p.case1_Rj << " m"
        << ", t0=" << t0 << " s"
        << ", amp_j=" << amp_j;
    result.log += oss.str() + "\n";

    MatrixXcd target_echo = generate_target_echo(p, lp);
    // 应用目标幅度和接收机噪声
    for (int k = 0; k < _nan1; ++k) {
        target_echo.col(k) *= p.case1_amp_target;
        target_echo.col(k) += awgn(target_echo.col(k), p.case1_awgn_snr);
    }
    MatrixXcd RDJ_Sig = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < p.case1_jj; ++k)
        result.echo_target.col(k) = target_echo.col(k);

    for (int k = p.case1_jj; k < _nan1; ++k) {
        VectorXcd sig = fft(target_echo.col(k - p.case1_jj));
        for (int i = 0; i < _nrn; ++i)
            sig(i) *= precise_expj_2pi_scalar(-lp.fr(i) * t0);
        sig = ifft(sig);
        RDJ_Sig.col(k) += sig;
        result.echo_target.col(k) = target_echo.col(k) + amp_j * RDJ_Sig.col(k);
    }
    result.jam_signal    = amp_j * RDJ_Sig;
    result.target_signal = target_echo;
}

// ═══════════════════════════════════════════════════════════════════
// Case 2: 速度假目标干扰 (VDJ)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case2(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double t0 = 2 * p.case2_Rj / c;
    double fj = 2 * p.case2_Vj / lp.lambda_val;

    ostringstream oss;
    oss << "[Case2 VDJ] Vj=" << p.case2_Vj << " m/s"
        << ", jj=" << p.case2_jj
        << ", Rj=" << p.case2_Rj << " m"
        << ", amp_j=" << p.case2_amp_j
        << ", fj=" << fj << " Hz";
    result.log += oss.str() + "\n";

    MatrixXcd target_echo = generate_target_echo(p, lp);
    // 应用目标幅度和接收机噪声
    for (int k = 0; k < _nan1; ++k) {
        target_echo.col(k) *= p.case2_amp_target;
        target_echo.col(k) += awgn(target_echo.col(k), p.case2_awgn_snr);
    }
    MatrixXcd VDJ_Sig = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < p.case2_jj; ++k)
        result.echo_target.col(k) = target_echo.col(k);

    for (int k = p.case2_jj; k < _nan1; ++k) {
        VectorXcd sig = target_echo.col(k - p.case2_jj);
        for (int i = 0; i < _nrn; ++i)
            sig(i) *= precise_expj_2pi_scalar(fj * lp.tnrn(i));
        sig = fft(sig);
        for (int i = 0; i < _nrn; ++i)
            sig(i) *= precise_expj_2pi_scalar(-lp.fr(i) * t0);
        sig = ifft(sig);
        VDJ_Sig.col(k) += sig;
        result.echo_target.col(k) = target_echo.col(k) + p.case2_amp_j * VDJ_Sig.col(k);
    }
    result.jam_signal    = p.case2_amp_j * VDJ_Sig;
    result.target_signal = target_echo;
}

// ═══════════════════════════════════════════════════════════════════
// Case 3: 间歇采样转发干扰 (ISRJ)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case3(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double T_ISRJ = (p.case3_T_ISRJ == 0.0) ? p.case3_Ts_ISRJ / 4.0 : p.case3_T_ISRJ;
    double t0 = 2 * p.case3_Rj / c;
    double amp_j = p.case3_amp_j;
    int Ts_Num  = static_cast<int>(p.case3_Ts_ISRJ * lp.fs_val);
    int T_Num   = static_cast<int>(T_ISRJ * lp.fs_val);
    int N_ISRJ  = static_cast<int>(p.Tp / p.case3_Ts_ISRJ);
    int Num_C_I = static_cast<int>(p.case3_Ts_ISRJ / T_ISRJ) - 1;

    ostringstream oss;
    oss << "[Case3 ISRJ] Ts_ISRJ=" << p.case3_Ts_ISRJ
        << ", T_ISRJ=" << T_ISRJ
        << ", Rj=" << p.case3_Rj << " m"
        << ", amp_j=" << amp_j
        << ", N_ISRJ=" << N_ISRJ
        << ", Num_C_I=" << Num_C_I;
    result.log += oss.str() + "\n";

    MatrixXcd target_echo = generate_target_echo(p, lp);
    // 应用目标幅度和接收机噪声
    for (int k = 0; k < _nan1; ++k) {
        target_echo.col(k) *= p.case3_amp_target;
        target_echo.col(k) += awgn(target_echo.col(k), p.case3_awgn_snr);
    }
    MatrixXcd ISRJ_Sig = MatrixXcd::Zero(_nrn, _nan1);

    for (int m = 0; m < _nan1; ++m) {
        // 寻找有效区域起始位置
        int start_index = -1;
        for (int i = 0; i < _nrn; ++i) {
            if (abs(target_echo(i, m)) > 1e-12) { start_index = i; break; }
        }
        if (start_index == -1) continue;

        // 间歇采样窗口
        VectorXd win = VectorXd::Zero(_nrn);
        for (int Ni = 0; Ni < N_ISRJ; ++Ni) {
            int idx_start = start_index + Ni * Ts_Num;
            int idx_end   = start_index + T_Num + Ni * Ts_Num;
            for (int idx = idx_start; idx < idx_end && idx < _nrn; ++idx)
                win(idx) = 1.0;
        }

        // 截取采样段信号
        VectorXcd sig(_nrn);
        for (int i = 0; i < _nrn; ++i)
            sig(i) = target_echo(i, m) * win(i);

        // 多普勒切片重组
        for (int Ni = 0; Ni < Num_C_I; ++Ni) {
            VectorXcd sig0 = fft(sig);
            double doppler_delay = (Ni + 1) * T_ISRJ;
            for (int i = 0; i < _nrn; ++i)
                sig0(i) *= precise_expj_2pi_scalar(-lp.fr(i) * doppler_delay);
            sig0 = ifft(sig0);
            for (int i = 0; i < _nrn; ++i)
                ISRJ_Sig(i, m) += sig0(i);
        }

        // 引入距离时延
        VectorXcd temp = fft(ISRJ_Sig.col(m));
        for (int i = 0; i < _nrn; ++i)
            temp(i) *= precise_expj_2pi_scalar(-lp.fr(i) * t0);
        temp = ifft(temp);
        ISRJ_Sig.col(m) = temp;
    }

    result.echo_target  = amp_j * ISRJ_Sig + target_echo;
    result.jam_signal    = amp_j * ISRJ_Sig;
    result.target_signal = target_echo;
}

// ═══════════════════════════════════════════════════════════════════
// Case 4: 窄带噪声干扰 (NNJ)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case4(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double power_linear = pow(10.0, p.case4_power_dBW / 10.0);
    double sigma = sqrt(power_linear / 2.0);

    static default_random_engine generator((unsigned)random_device{}());
    normal_distribution<double> normal_dist(0.0, 1.0);

    ostringstream oss;
    oss << "[Case4 NNJ] power=" << p.case4_power_dBW << " dBW"
        << ", sigma=" << sigma
        << ", butter_order=" << p.case4_butter_order
        << ", cutoff=" << p.case4_butter_cutoff;
    result.log += oss.str() + "\n";

    MatrixXcd fft_lvbo_z1 = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd NNJ = MatrixXcd::Zero(_nrn, _nan1);

    // Butterworth 滤波器 (只设计一次)
    vector<double> b_filt, a_filt;
    butter(p.case4_butter_order, p.case4_butter_cutoff, b_filt, a_filt);

    for (int m = 0; m < _nan1; ++m) {
        ArrayXcd carrier = precise_expj_2pi_array(p.fc, lp.tnrn.array(), lp.fs_val, lp.Tstart_val);
        VectorXcd z(_nrn);
        for (int i = 0; i < _nrn; ++i)
            z(i) = complex<double>(normal_dist(generator) * sigma,
                                   normal_dist(generator) * sigma) * carrier(i);

        VectorXcd lvbo_z = filter(b_filt, a_filt, z);
        VectorXcd fft_lvbo_z = fft(lvbo_z);

        // 找最小幅值
        int minIndex = 0;
        double minVal = abs(fft_lvbo_z(0));
        for (int i = 1; i < _nrn; ++i) {
            double mag = abs(fft_lvbo_z(i));
            if (mag < minVal) { minVal = mag; minIndex = i; }
        }
        // 抑制高频分量
        for (int i = _nrn / 2; i < _nrn; ++i)
            fft_lvbo_z(i) = fft_lvbo_z(minIndex);

        fft_lvbo_z1.col(m) = fft_lvbo_z;
    }

    for (int m = 0; m < _nan1; ++m)
        NNJ.col(m) = ifft(fft_lvbo_z1.col(m));

    // 独立生成目标回波 (与 Case5~10 一致)
    double phi0 = acos(p.z_R0 / p.Rs);
    double x0 = p.Rs * sin(phi0) * sin(0.0);
    double y0 = p.Rs * sin(phi0) * cos(0.0);
    double R0 = Vector3d(x0, y0, 0.0).norm();

    MatrixXcd target_Sig = MatrixXcd::Zero(_nrn, _nan1);
    for (int k = 0; k < _nan1; ++k) {
        double Rt = R0 - p.Vr * k / p.prf * p.range_walk_factor;
        VectorXd td = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd phase_t = (I_complex * PI * lp.gama_val * td.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val);
        Array<bool, -1, 1> wint = (td.array() >= -p.Tp / 2.0) && (td.array() <= p.Tp / 2.0);
        VectorXcd reT = wint.cast<double>() * p.case4_amp_target * phase_t.array();
        reT += awgn(reT, p.case4_awgn_snr);
        target_Sig.col(k) = reT;
    }

    result.echo_target  = NNJ + target_Sig;
    result.jam_signal    = NNJ;
    result.target_signal = target_Sig;
}

// ═══════════════════════════════════════════════════════════════════
// Case 5: 距离波门拖引干扰 (RGPO)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case5(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;

    double phi0 = acos(p.z_R0 / p.Rs);
    double x0 = p.Rs * sin(phi0) * sin(0.0);
    double y0 = p.Rs * sin(phi0) * cos(0.0);
    double R0 = Vector3d(x0, y0, 0.0).norm();

    ostringstream oss;
    oss << "[Case5 RGPO] Vj=" << p.case5_Vj
        << ", amp_target=" << p.case5_amp_target
        << ", amp_jammer=" << p.case5_amp_jammer
        << ", drag_stages=" << p.case5_drag_stages
        << ", R0=" << R0 << " m";
    result.log += oss.str() + "\n";

    MatrixXcd JRGPO_Sig = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd target_Sig = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < _nan1; ++k) {
        // 目标回波
        double Rt = R0 - p.Vr * k / p.prf * p.range_walk_factor;
        VectorXd td = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd phase_t = (I_complex * PI * lp.gama_val * td.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val);
        Array<bool, -1, 1> wint = (td.array() >= -p.Tp / 2.0) && (td.array() <= p.Tp / 2.0);
        VectorXcd reT = wint.cast<double>() * p.case5_amp_target * phase_t.array();
        reT += awgn(reT, p.case5_awgn_snr);
        target_Sig.col(k) = reT;

        // 干扰参数 (分阶段)
        double Rj;
        if (k == 0)
            Rj = R0 - p.Vr * k / p.prf * p.range_walk_factor - p.case5_Vj * (k + 0.01) / p.prf * p.range_walk_factor;
        else if (k < p.case5_drag_stages)
            Rj = R0 - p.Vr * k / p.prf * p.range_walk_factor - p.case5_Vj * k / p.prf * p.range_walk_factor;
        else
            Rj = R0 - p.Vr * k / p.prf * p.range_walk_factor + p.case5_Vj * (k - p.case5_drag_stages + 1) / p.prf * p.range_walk_factor;

        // 干扰信号
        VectorXd tdj = lp.tnrn.array() - 2 * Rj / c;
        VectorXcd phase_j = (I_complex * PI * lp.gama_val * tdj.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * Rj / lp.lambda_val);
        Array<bool, -1, 1> winj = (tdj.array() >= -p.Tp / 2.0) && (tdj.array() <= p.Tp / 2.0);
        VectorXcd reJ = winj.cast<double>() * p.case5_amp_jammer * phase_j.array();
        reJ += awgn(reJ, p.case5_awgn_snr);

        JRGPO_Sig.col(k) = reJ;
        result.echo_target.col(k) = target_Sig.col(k) + JRGPO_Sig.col(k);
    }

    result.jam_signal    = JRGPO_Sig;
    result.target_signal = target_Sig;
}

// ═══════════════════════════════════════════════════════════════════
// Case 6: 速度波门拖引干扰 (VGPO)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case6(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double fd = 2 * p.Vr / lp.lambda_val;

    double phi0 = acos(p.z_R0 / p.Rs);
    double x0 = p.Rs * sin(phi0) * sin(0.0);
    double y0 = p.Rs * sin(phi0) * cos(0.0);
    double R0 = Vector3d(x0, y0, 0.0).norm();

    ostringstream oss;
    oss << "[Case6 VGPO] Vj=" << p.case6_Vj
        << ", amp_target=" << p.case6_amp_target
        << ", amp_jammer=" << p.case6_amp_jammer
        << ", drag_stages=" << p.case6_drag_stages
        << ", fd=" << fd << " Hz"
        << ", R0=" << R0 << " m";
    result.log += oss.str() + "\n";

    MatrixXcd JVGPO_Sig = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd target_Sig = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < _nan1; ++k) {
        double Rt = R0 - p.Vr * (k + 1) / p.prf;

        // 目标回波 (含多普勒速度相位)
        VectorXd td = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd phase_t = (I_complex * PI * lp.gama_val * td.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val)
                          * (I_complex * 2.0 * PI * fd * td.array()).exp();
        Array<bool, -1, 1> wint = (td.array() >= -p.Tp / 2.0) && (td.array() <= p.Tp / 2.0);
        VectorXcd reT = wint.cast<double>() * p.case6_amp_target * phase_t.array();
        reT += awgn(reT, p.case6_awgn_snr);
        target_Sig.col(k) = reT;

        // 干扰参数 (分阶段速度拖引)
        double fj;
        if (k == 0)
            fj = 2 * p.case6_Vj / lp.lambda_val * (k + 0.001) / p.prf * p.case6_velocity_drag_factor;
        else if (k < p.case6_drag_stages)
            fj = 2 * p.case6_Vj / lp.lambda_val * k / p.prf * p.case6_velocity_drag_factor;
        else
            fj = 2 * p.case6_Vj / lp.lambda_val * (k - p.case6_drag_stages + 1) / p.prf * p.case6_velocity_drag_factor;

        // 干扰信号
        VectorXd tdj = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd phase_j = (I_complex * PI * lp.gama_val * tdj.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val)
                          * (I_complex * 2.0 * PI * (fd + fj) * tdj.array()).exp();
        Array<bool, -1, 1> winj = (tdj.array() >= -p.Tp / 2.0) && (tdj.array() <= p.Tp / 2.0);
        VectorXcd reJ = winj.cast<double>() * p.case6_amp_jammer * phase_j.array();
        reJ += awgn(reJ, p.case6_awgn_snr);

        JVGPO_Sig.col(k) = reJ;
        result.echo_target.col(k) = target_Sig.col(k) + JVGPO_Sig.col(k);
    }

    result.jam_signal    = JVGPO_Sig;
    result.target_signal = target_Sig;
}

// ═══════════════════════════════════════════════════════════════════
// Case 7: 密集假目标干扰 (DRFTJ)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case7(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double amp_jammer = p.case7_amp_target * pow(10, p.case7_JSR / 20.0);

    double phi0 = acos(p.z_R0 / p.Rs);
    double x0 = p.Rs * sin(phi0) * sin(0.0);
    double y0 = p.Rs * sin(phi0) * cos(0.0);
    double R0 = Vector3d(x0, y0, 0.0).norm();

    ostringstream oss;
    oss << "[Case7 DRFTJ] JSR=" << p.case7_JSR << " dB"
        << ", num_jam=" << p.case7_num_jam
        << ", detaR=" << p.case7_detaR << " m"
        << ", amp_jammer=" << amp_jammer
        << ", R0=" << R0 << " m";
    result.log += oss.str() + "\n";

    MatrixXcd JMT_Sig   = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd Unj_Sig   = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target  = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < _nan1; ++k) {
        double Rt = R0 - p.Vr * k / p.prf;
        VectorXd td = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd phase_t = (I_complex * PI * lp.gama_val * td.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val);
        Array<bool, -1, 1> wint = (td.array() >= 0) && (td.array() <= p.Tp);
        VectorXcd reT = wint.cast<double>() * p.case7_amp_target * phase_t.array();
        reT += awgn(reT, p.case7_awgn_snr);
        Unj_Sig.col(k) += reT;

        for (int jn = 0; jn < p.case7_num_jam; ++jn) {
            double Rj;
            if (jn < p.case7_forward_replicas)
                Rj = R0 - p.Vr * k / p.prf + (jn + 1) * p.case7_detaR;
            else
                Rj = R0 - p.Vr * k / p.prf - (jn - 2) * p.case7_detaR;

            VectorXd tdj = lp.tnrn.array() - 2 * Rj / c;
            VectorXcd phase_j = (I_complex * PI * lp.gama_val * tdj.array().square()).exp()
                              * precise_expj_2pi_scalar(-2.0 * Rj / lp.lambda_val);
            Array<bool, -1, 1> winj = (tdj.array() >= 0) && (tdj.array() <= p.Tp);
            VectorXcd reJ = winj.cast<double>() * amp_jammer * phase_j.array();
            reJ += awgn(reJ, p.case7_awgn_snr);
            JMT_Sig.col(k) += reJ;
        }
        result.echo_target.col(k) = JMT_Sig.col(k) + Unj_Sig.col(k);
    }

    result.jam_signal    = JMT_Sig;
    result.target_signal = Unj_Sig;
}

// ═══════════════════════════════════════════════════════════════════
// Case 8: 脉内前沿切片重复干扰 (IPLESRJ)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case8(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nan1 = p.nan1;
    double T_ISRJ   = p.Tp / p.case8_T_ISRJ_ratio;
    double R0       = p.case8_R0_new;
    double Kr       = p.B / p.Tp;
    double amp_jammer = p.case8_amp_target * pow(10, p.case8_A_RJ / 20.0);
    int N_ISRJ      = static_cast<int>(p.Tp / T_ISRJ);

    // Case8 使用独立的距离向采样参数
    int nrn_new = 2 * static_cast<int>(floor((lp.fs_val * p.Tp + p.case8_R0_new) / 2.0));
    VectorXd tnrn_new = (VectorXd::LinSpaced(nrn_new, 0, nrn_new - 1) / lp.fs_val).array()
                        + p.case8_R0_new / c;

    ostringstream oss;
    oss << "[Case8 IPLESRJ] A_RJ=" << p.case8_A_RJ << " dB"
        << ", R0_new=" << p.case8_R0_new << " m"
        << ", V_ISRJ=" << p.case8_V_ISRJ << " m/s"
        << ", T_ISRJ=" << T_ISRJ << " s"
        << ", N_ISRJ=" << N_ISRJ
        << ", nrn_new=" << nrn_new
        << ", amp_jammer=" << amp_jammer;
    result.log += oss.str() + "\n";

    MatrixXcd IPLESRJ  = MatrixXcd::Zero(nrn_new, _nan1);
    MatrixXcd Unj_Sig  = MatrixXcd::Zero(nrn_new, _nan1);
    result.echo_target = MatrixXcd::Zero(nrn_new, _nan1);

    for (int k = 0; k < _nan1; ++k) {
        // 目标回波
        double R_t = R0 - p.Vr * (k + 1) / p.prf;
        VectorXd td = tnrn_new.array() - 2 * R_t / c;
        VectorXcd phase_t = (I_complex * PI * Kr * td.array().square()).exp()
                          * precise_expj_2pi_scalar(-2.0 * p.fc * R_t / c);
        Array<bool, -1, 1> win = (td.array() < p.Tp) && (td.array() > 0);
        VectorXcd reT = win.cast<double>() * p.case8_amp_target * phase_t.array();
        reT += awgn(reT, p.case8_awgn_snr);
        Unj_Sig.col(k) = reT;

        // 干扰信号
        double R_ISRJ = R0 - p.case8_V_ISRJ * (k + 1) / p.prf - p.case8_R_ahead;
        VectorXcd s_ISRJ2 = VectorXcd::Zero(nrn_new);
        for (int Ni = 0; Ni < N_ISRJ; ++Ni) {
            VectorXd tdj = tnrn_new.array() - 2 * R_ISRJ / c - Ni * T_ISRJ;
            VectorXcd phase_j = (I_complex * PI * Kr * tdj.array().square()).exp()
                              * precise_expj_2pi_scalar(-2.0 * p.fc * R_ISRJ / c - p.fc * Ni * T_ISRJ);
            Array<bool, -1, 1> win1  = (tdj.array() < T_ISRJ) && (tdj.array() > 0);
            Array<bool, -1, 1> winjj = (tdj.array() < p.Tp) && (tdj.array() > 0);
            VectorXcd s_ISRJ1 = (win1 && winjj).cast<double>() * amp_jammer * phase_j.array();
            s_ISRJ1 += awgn(s_ISRJ1, p.case8_awgn_snr);
            s_ISRJ2 += s_ISRJ1;
        }
        IPLESRJ.col(k) = s_ISRJ2;
        result.echo_target.col(k) = Unj_Sig.col(k) + IPLESRJ.col(k);
    }

    result.jam_signal    = IPLESRJ;
    result.target_signal = Unj_Sig;
    result.nrn = nrn_new;  // Case8 使用不同的 nrn
}

// ═══════════════════════════════════════════════════════════════════
// Case 9: 频谱弥散干扰 (SMSP)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case9(const JammingParams& p, const LocalParams& lp,
                          JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    double gama_j   = p.case9_num_slices * lp.gama_val;
    double Tp_j     = p.Tp / p.case9_num_slices;
    double amp_jammer = p.case9_amp_target * pow(10, p.case9_JSR / 20.0) * p.case9_amp_extra;

    ostringstream oss;
    oss << "[Case9 SMSP] num_slices=" << p.case9_num_slices
        << ", JSR=" << p.case9_JSR << " dB"
        << ", R0=" << p.case9_R0 << " m"
        << ", gama_j=" << gama_j
        << ", Tp_j=" << Tp_j
        << ", amp_jammer=" << amp_jammer;
    result.log += oss.str() + "\n";

    MatrixXcd JSMSP_Sig = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd Unj_Sig   = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target  = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < _nan1; ++k) {
        double Rt = p.case9_R0 - p.Vr * k / p.prf;

        // 目标回波
        VectorXd td = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd chirp_phase = (I_complex * PI * lp.gama_val * td.array().square()).exp();
        complex<double> dist_phase = precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val);
        Array<bool, -1, 1> wint = (td.array() >= 0) && (td.array() <= p.Tp);
        VectorXcd reT = wint.cast<double>() * p.case9_amp_target * chirp_phase.array() * dist_phase;
        reT += awgn(reT, p.case9_awgn_snr);
        Unj_Sig.col(k) = reT;

        // SMSP 干扰 (增强调频率切片)
        double Rj = Rt;
        VectorXcd s_ISRJ = VectorXcd::Zero(_nrn);
        for (int jn = 0; jn < p.case9_num_slices; ++jn) {
            double t_offset = jn * Tp_j;
            VectorXd tdj = lp.tnrn.array() - 2 * Rj / c - t_offset;
            VectorXcd chirp_phase_j = (I_complex * PI * gama_j * tdj.array().square()).exp();
            complex<double> dist_phase_j = precise_expj_2pi_scalar(-2.0 * (Rj + c * t_offset / 2) / lp.lambda_val);
            Array<bool, -1, 1> winj = (tdj.array() >= 0) && (tdj.array() <= Tp_j);
            VectorXcd slice = winj.cast<double>() * amp_jammer * chirp_phase_j.array() * dist_phase_j;
            slice += awgn(slice, p.case9_awgn_snr);
            s_ISRJ += slice;
        }

        JSMSP_Sig.col(k) = s_ISRJ;
        result.echo_target.col(k) = Unj_Sig.col(k) + JSMSP_Sig.col(k);
    }

    result.jam_signal    = JSMSP_Sig;
    result.target_signal = Unj_Sig;
}

// ═══════════════════════════════════════════════════════════════════
// Case 10: 梳状谱干扰 (COMB)
// ═══════════════════════════════════════════════════════════════════

static void ganrao_case10(const JammingParams& p, const LocalParams& lp,
                           JammingResult& result)
{
    const int _nrn = lp.nrn_val, _nan1 = p.nan1;
    int mid_tones   = p.case10_num_tones / 2;
    double amp_jammer = p.case10_amp_target * pow(10, p.case10_JSR / 20.0);

    ostringstream oss;
    oss << "[Case10 COMB] num_tones=" << p.case10_num_tones
        << ", JSR=" << p.case10_JSR << " dB"
        << ", R0=" << p.case10_R0 << " m"
        << ", deltaf=" << p.case10_deltaf << " Hz"
        << ", amp_jammer=" << amp_jammer;
    result.log += oss.str() + "\n";

    MatrixXcd JCOMB_Sig = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd Unj_Sig   = MatrixXcd::Zero(_nrn, _nan1);
    result.echo_target  = MatrixXcd::Zero(_nrn, _nan1);

    for (int k = 0; k < _nan1; ++k) {
        double Rt = p.case10_R0 - p.Vr * k / p.prf;

        // 目标回波
        VectorXd td = lp.tnrn.array() - 2 * Rt / c;
        VectorXcd chirp_phase = (I_complex * PI * lp.gama_val * td.array().square()).exp();
        complex<double> dist_phase = precise_expj_2pi_scalar(-2.0 * Rt / lp.lambda_val);
        Array<bool, -1, 1> wint = (td.array() >= 0) && (td.array() <= p.Tp);
        VectorXcd reT = wint.cast<double>() * p.case10_amp_target * chirp_phase.array() * dist_phase;
        reT += awgn(reT, p.case10_awgn_snr);
        Unj_Sig.col(k) = reT;

        // 梳状谱干扰
        double Rj = Rt;
        VectorXcd s_comb = VectorXcd::Zero(_nrn);
        for (int jn = 0; jn < p.case10_num_tones; ++jn) {
            double fi = (jn <= mid_tones)
                ? ((jn + 1) * p.case10_deltaf)
                : (-(jn - mid_tones + 1) * p.case10_deltaf);

            VectorXd tdj = lp.tnrn.array() - 2 * Rj / c;
            VectorXcd chirp_freq_phase =
                (I_complex * PI * lp.gama_val * tdj.array().square()
                 - I_complex * 2.0 * PI * fi * tdj.array()).exp();
            complex<double> dist_phase_j = precise_expj_2pi_scalar(-2.0 * Rj / lp.lambda_val);
            Array<bool, -1, 1> winj = (tdj.array() >= 0) && (tdj.array() <= p.Tp);
            VectorXcd slice = winj.cast<double>() * amp_jammer
                            * chirp_freq_phase.array() * dist_phase_j;
            slice += awgn(slice, p.case10_awgn_snr);
            s_comb += slice;
        }

        JCOMB_Sig.col(k) = s_comb;
        result.echo_target.col(k) = Unj_Sig.col(k) + JCOMB_Sig.col(k);
    }

    result.jam_signal    = JCOMB_Sig;
    result.target_signal = Unj_Sig;
}

// ═══════════════════════════════════════════════════════════════════
// 主入口函数
// ═══════════════════════════════════════════════════════════════════

JammingResult generate_jamming(int mode, const JammingParams& params)
{
    JammingResult result;
    ostringstream header;
    header << "═══ 干扰生成 mode=" << mode << " ═══\n"
           << "  fc=" << params.fc << " Hz, Tp=" << params.Tp << " s"
           << ", B=" << params.B << " Hz, prf=" << params.prf << " Hz\n"
           << "  Vr=" << params.Vr << " m/s, Rs=" << params.Rs << " m"
           << ", nan1=" << params.nan1 << "\n";
    result.log += header.str();

    // 计算局部派生参数
    LocalParams lp;
    lp.compute(params);
    result.nrn  = lp.nrn_val;
    result.nan1 = params.nan1;

    switch (mode) {
        case 1:  ganrao_case1(params, lp, result);  break;
        case 2:  ganrao_case2(params, lp, result);  break;
        case 3:  ganrao_case3(params, lp, result);  break;
        case 4:  ganrao_case4(params, lp, result);  break;
        case 5:  ganrao_case5(params, lp, result);  break;
        case 6:  ganrao_case6(params, lp, result);  break;
        case 7:  ganrao_case7(params, lp, result);  break;
        case 8:  ganrao_case8(params, lp, result);  break;
        case 9:  ganrao_case9(params, lp, result);  break;
        case 10: ganrao_case10(params, lp, result); break;
        default:
            result.log += "[ERROR] 无效模式: " + to_string(mode) + " (有效范围 1~10)\n";
            return result;
    }

    result.log += "  完成: echo_target(" + to_string(result.echo_target.rows())
                + "x" + to_string(result.echo_target.cols()) + ")\n";

    return result;
}
