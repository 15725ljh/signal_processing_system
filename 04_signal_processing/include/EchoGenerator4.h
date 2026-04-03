// ============================================================================
//  EchoGenerator4.h — 回波+干扰信号生成器 (从03模块移植,适配04模块参数体系)
//  Echo Generator for Module04 — ported from Module03, adapted for Module04
// ============================================================================
//
//  本文件以 03_jamming_detection_suppression/include/EchoGenerator.h 为蓝本,
//  保留全部算法逻辑,仅做以下适配:
//    1. 使用 04模块 parameters.h 中的参数函数 (fc(), B(), fs(), prf(), etc.)
//    2. 依赖 JammingSimulator4.h (本模块自有的干扰模拟器)
//    3. 输出格式: MatrixXcd (cpiNum × nrn), 每行一个CPI, 由调用方负责转置
//
//  算法原理(与03模块完全一致):
//    1. 生成LFM目标回波 (含载波调制)
//    2. 添加AWGN噪声得到 s_echo_noise
//    3. 用 JammingSimulator4 生成指定类型干扰
//    4. 按 JSR 归一化干扰幅度并叠加
//    5. 添加底噪得到最终混合回波
//
//  依赖: Eigen/Dense, JammingSimulator4.h, parameters.h
//
// ============================================================================

#ifndef MODULE4_ECHO_GENERATOR_H
#define MODULE4_ECHO_GENERATOR_H

#include "JammingSimulator4.h"
#include "parameters.h"
#include <random>

// CFG 宏

/**
 * @brief 存储回波生成结果的结构体
 */
struct EchoGeneratorResult4 {
    MatrixXcd echo_signal;     // 最终混合回波 (cpiNum × nrn), 每行一个CPI
    RowVectorXcd s_echo_noise; // 原始带噪目标回波 (1 × nrn), 未与干扰混合
};

/**
 * @brief 内部 AWGN 函数
 */
inline RowVectorXcd addAwgn4(const RowVectorXcd& signal, double snr_dB) {
    double signal_power = signal.squaredNorm() / signal.size();
    if (signal_power < 1e-14) return RowVectorXcd::Zero(signal.size());

    double noise_power = signal_power / pow(10.0, snr_dB / 10.0);
    std::mt19937 rng{std::random_device{}()};
    std::normal_distribution<double> dist(0.0, sqrt(noise_power / 2.0));

    RowVectorXcd noise(signal.size());
    for (long i = 0; i < signal.size(); ++i) noise(i) = {dist(rng), dist(rng)};
    return signal + noise;
}

/**
 * @brief 生成含干扰的回波信号
 *
 * @param cpiNum    CPI数量(脉冲数)
 * @param realLabel 干扰类型: 1=ISDJ, 2=ISRJ, 3=ISCJ, 4=NBJ, 5=RDJ
 * @return EchoGeneratorResult4
 */
inline EchoGeneratorResult4 generateEcho4(int cpiNum, int realLabel) {
    // ── 1. 参数定义 (使用04模块的 parameters.h 函数) ──
    const double _fc  = fc();
    const double _B   = B();
    const double _fs  = fs();
    const double _Tp  = Tp();
    const double _Kr  = _B / _Tp;
    const double _R0  = Rs();             // 目标初始距离
    const int    _nrn = nrn();
    const double _c   = c;
    const double _prf_val = prf();
    const double _Vr_val  = Vr();

    // JSR/SNR 参数 (与03模块一致)
    const double SNR = CFG.getDouble("detection_suppression.SNR", 25.0);
    const double JSR = CFG.getDouble("detection_suppression.JSR", 30.0);
    const double noise_snr = CFG.getDouble("detection_suppression.noise_snr", 25.0);
    const double r_min_ratio = CFG.getDouble("detection_suppression.r_min_ratio", 0.7);
    const double r_max_ratio = CFG.getDouble("detection_suppression.r_max_ratio", 1.7);

    // ── 2. 生成纯净目标回波 (s_echo) ──
    // 时间轴(适配04模块): tnrn(i) = Tstart + i/fs
    // 04模块采样窗口以 2*R0/c 为中心: Tstart = 2*R0/c - nrn/(2*fs)
    double Tstart = 2.0 * _R0 / _c - _nrn / 2.0 / _fs;
    VectorXd tnrn_local = VectorXd::LinSpaced(_nrn, 0, _nrn - 1).array() / _fs + Tstart;
    ArrayXd term = tnrn_local.array() - 2 * _R0 / _c;
    // 脉冲窗口: 0 < term < Tp
    Array<bool, -1, 1> win = (term > 0) && (term < _Tp);
    const std::complex<double> j(0.0, 1.0);
    // LFM + 载波相位
    ArrayXcd phase = j * M_PI * _Kr * term.pow(2) + j * 2.0 * M_PI * _fc * term;
    const RowVectorXcd s_echo = win.select(phase.exp(), 0.0);

    // ── 3. 添加AWGN噪声 ──
    const RowVectorXcd original_s_echo_noise = addAwgn4(s_echo, SNR);

    // ── 4. 初始化输出矩阵 ──
    MatrixXcd final_echo_matrix = MatrixXcd::Zero(cpiNum, _nrn);
    const double A = pow(10.0, JSR / 20.0);
    const double s_echo_noise_max_abs = original_s_echo_noise.cwiseAbs().maxCoeff();

    // ── 5. 随机干扰距离(自适应采样窗口) ──
    // 04模块采样窗口以 2*R0/c 为中心, 半宽 nrn/(2*fs)
    // 干扰可见条件: 2*R1/c 在 [Tstart-Tp, Tend] 内
    // 即 R1 ∈ [R0 - c*nrn/(4*fs) - c*Tp/2, R0 + c*nrn/(4*fs)]
    std::mt19937 rng{std::random_device{}()};
    double R1_visible_min = _R0 - _c * _nrn / (4.0 * _fs) - _c * _Tp / 2.0;
    double R1_visible_max = _R0 + _c * _nrn / (4.0 * _fs);
    // 与配置比例范围取交集,确保不超出配置范围
    double R1_lo = max(r_min_ratio * _R0, R1_visible_min);
    double R1_hi = min(r_max_ratio * _R0, R1_visible_max);
    if (R1_lo >= R1_hi) { R1_lo = r_min_ratio * _R0; R1_hi = r_max_ratio * _R0; }
    double dis_R = (R1_hi - R1_lo) / cpiNum;
    VectorXd R1_options = VectorXd::LinSpaced(cpiNum, R1_lo, R1_hi - dis_R);
    std::uniform_int_distribution<int> r1_dist(0, cpiNum - 1);

    // ── 6. 逐脉冲生成干扰并混合 ──
    for (int num = 0; num < cpiNum; ++num) {
        double R1_random = R1_options(r1_dist(rng));
        JammingSimulator4 jammer(_fc, _B, _fs, _prf_val, _Tp, 1, _R0, R1_random, _nrn);
        MatrixXcd jammingsignal_matrix;

        switch (realLabel) {
            case 1: jammingsignal_matrix = jammer.generateISDJ();   break;
            case 2: jammingsignal_matrix = jammer.generateISRJ();   break;
            case 3: jammingsignal_matrix = jammer.generateISCJ();   break;
            case 4: jammingsignal_matrix = jammer.generateNBJ();    break;
            case 5: jammingsignal_matrix = jammer.generateRDJ();    break;
            default: jammingsignal_matrix = MatrixXcd::Zero(1, _nrn); break;
        }

        const RowVectorXcd jammingsignal_this_pulse = s_echo_noise_max_abs * A * jammingsignal_matrix.row(0);
        const RowVectorXcd mixed_signal = jammingsignal_this_pulse + original_s_echo_noise;
        final_echo_matrix.row(num) = addAwgn4(mixed_signal, noise_snr);
    }

    return {final_echo_matrix, original_s_echo_noise};
}

#endif // MODULE4_ECHO_GENERATOR_H
