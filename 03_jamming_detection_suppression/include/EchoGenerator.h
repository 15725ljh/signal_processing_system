#ifndef ECHO_GENERATOR_H // 防止头文件被重复包含
#define ECHO_GENERATOR_H // 定义宏，表示头文件已被包含

#include "JammingSimulator.h"
#include "Config.h"
#define CFG Config::instance()
#include <random>

/**
 * @brief 存储回波生成结果的结构体
 * 包含最终的混合回波信号矩阵和原始带噪回波信号。
 */
struct EchoGeneratorResult {
    MatrixXcd echo_signal;    // 最终生成的混合回波信号矩阵，每行代表一个脉冲的信号
    RowVectorXcd s_echo_noise; // 原始的带噪目标回波信号，未与干扰混合
};

/**
 * @brief 内部使用的 AWGN (Additive White Gaussian Noise) 函数
 * 为信号添加指定信噪比的加性高斯白噪声。
 * @param signal 输入的原始信号 (复数行向量)。
 * @param snr_dB 信噪比 (以分贝为单位)。
 * @return 添加噪声后的信号 (复数行向量)。
 */
inline RowVectorXcd addAwgn_final(const RowVectorXcd& signal, double snr_dB) {
    double signal_power = signal.squaredNorm() / signal.size(); // 计算信号功率
    // 如果信号功率过小，直接返回零向量，避免除以零或产生无效噪声
    if (signal_power < 1e-14) return RowVectorXcd::Zero(signal.size());

    // 根据信噪比计算噪声功率
    double noise_power = signal_power / pow(10.0, snr_dB / 10.0);

    std::mt19937 rng{std::random_device{}()}; // 初始化随机数生成器
    // 创建正态分布，均值为0，标准差根据噪声功率计算
    std::normal_distribution<double> dist(0.0, sqrt(noise_power / 2.0));

    RowVectorXcd noise(signal.size()); // 创建噪声向量
    // 为噪声向量的实部和虚部生成随机高斯噪声
    for (long i = 0; i < signal.size(); ++i) noise(i) = {dist(rng), dist(rng)};

    return signal + noise; // 返回原始信号与噪声的和
}

/**
 * @brief 根据输入参数：回波脉冲数量、干扰标签生成回波和干扰信号
 * 该函数模拟雷达回波信号，并根据指定的干扰类型生成相应的干扰信号，
 * 最后将回波信号与干扰信号混合，并添加噪声。
 * @param cpiNum 相干处理间隔 (CPI) 的数量，即要生成的脉冲数量。
 * @param realLabel 干扰类型标签 (1: ISDJ, 2: ISRJ, 3: ISCJ, 4: NBJ, 5: RDJ)。
 * @return EchoGeneratorResult 结构体，包含最终的混合信号矩阵和原始带噪回波。
 */
inline EchoGeneratorResult generateEcho(int cpiNum, int realLabel) {
    // 1. 参数定义
    const double fc = CFG.getDouble("detection_suppression.fc", 35e9);
    const double B = CFG.getDouble("detection_suppression.B", 80e6);
    const double fs = CFG.getDouble("detection_suppression.fs", 120e6);
    const double R0 = CFG.getDouble("detection_suppression.R0", 1e3);
    const double prf = CFG.getDouble("detection_suppression.prf", 5e3);
    const double c = 3e8;
    const double Tp = CFG.getDouble("detection_suppression.Tp", 12e-6);
    const double Kr = B / Tp;
    const double SNR = CFG.getDouble("detection_suppression.SNR", 25.0);
    const double JSR = CFG.getDouble("detection_suppression.JSR", 30.0);
    const int nrn = CFG.getInt("detection_suppression.nrn", 2048);
    const double noise_snr = CFG.getDouble("detection_suppression.noise_snr", 25.0);
    const double r_min_ratio = CFG.getDouble("detection_suppression.r_min_ratio", 0.7);
    const double r_max_ratio = CFG.getDouble("detection_suppression.r_max_ratio", 1.7);

    // 2. 生成纯净目标回波 (s_echo)
    // 计算时间轴，考虑目标初始距离
    VectorXd tnrn = VectorXd::LinSpaced(nrn, 0, nrn - 1).array() / fs + R0 / c;
    ArrayXd term = tnrn.array() - 2 * R0 / c; // 计算时间项
    // 定义脉冲窗口：在脉冲宽度 Tp 内的信号有效
    Array<bool, -1, 1> win = (term > 0) && (term < Tp);
    const std::complex<double> j(0.0, 1.0); // 虚数单位 j
    // 计算信号相位：包含线性调频和载波相位
    ArrayXcd phase = j * M_PI * Kr * term.pow(2) + j * 2.0 * M_PI * fc * term;
    // 根据窗口选择有效信号部分，窗口外为0
    const RowVectorXcd s_echo = win.select(phase.exp(), 0.0);

    // 3. 生成原始带噪回波 (original_s_echo_noise)
    // 为纯净回波添加 AWGN 噪声，并设为 const 防止后续修改
    const RowVectorXcd original_s_echo_noise = addAwgn_final(s_echo, SNR);

    // 4. 初始化最终的输出矩阵 (final_echo_matrix)
    // 该矩阵将存储每个 CPI 的混合信号
    MatrixXcd final_echo_matrix = MatrixXcd::Zero(cpiNum, nrn);
    const double A = pow(10.0, JSR / 20.0); // 干扰信号幅度因子
    // 获取原始带噪回波的最大绝对值，用于干扰信号的归一化
    const double s_echo_noise_max_abs = original_s_echo_noise.cwiseAbs().maxCoeff();

    // 5. 初始化随机数生成器，用于干扰距离的随机选择
    std::mt19937 rng{std::random_device{}()};
    // 计算干扰距离的步长
    double dis_R = (r_max_ratio * R0 - r_min_ratio * R0) / cpiNum;
    VectorXd R1_options = VectorXd::LinSpaced(cpiNum, r_min_ratio * R0, r_max_ratio * R0 - dis_R);
    // 创建均匀整数分布，用于随机选择 R1_options 中的索引
    std::uniform_int_distribution<int> r1_dist(0, cpiNum - 1);

    // 6. 循环生成每个脉冲的信号
    // 遍历每个 CPI，生成对应的干扰信号并与回波混合
    for (int num = 0; num < cpiNum; ++num) {
        // 为当前脉冲随机选择一个干扰距离
        double R1_random = R1_options(r1_dist(rng));
        // 初始化干扰模拟器
        JammingSimulator jammer(fc, B, fs, prf, Tp, 1, R0, R1_random);
        MatrixXcd jammingsignal_matrix; // 存储生成的干扰信号矩阵

        // 根据 realLabel (干扰类型标签) 选择生成不同的干扰信号
        switch(realLabel) {
            case 1: jammingsignal_matrix = jammer.generateISDJ();   break; // 间歇直接转发干扰 (ISDJ)
            case 2: jammingsignal_matrix = jammer.generateISRJ();   break; // 间歇重复转发干扰 (ISRJ)
            case 3: jammingsignal_matrix = jammer.generateISCJ();   break; // 间歇循环转发干扰 (ISCJ)
            case 4: jammingsignal_matrix = jammer.generateNBJ();    break; // 窄带瞄频干扰 (NBJ)，带宽设置为 10 MHz
            case 5: jammingsignal_matrix = jammer.generateRDJ();    break; // 距离欺骗干扰 (RDJ)
            default: jammingsignal_matrix = MatrixXcd::Zero(1, nrn);break; // 默认情况：生成零干扰信号
        }
        // 根据 JSR 归一化干扰信号幅度，并获取当前脉冲的干扰信号
        const RowVectorXcd jammingsignal_this_pulse = s_echo_noise_max_abs * A * jammingsignal_matrix.row(0);

        // 将生成的干扰信号与原始带噪回波混合
        const RowVectorXcd mixed_signal = jammingsignal_this_pulse + original_s_echo_noise;

        // 为混合信号添加最终的底噪，并存入最终的输出矩阵
        final_echo_matrix.row(num) = addAwgn_final(mixed_signal, noise_snr);
    }

    // 7. 返回最终的混合信号矩阵，和从未被修改过的原始带噪回波
    return {final_echo_matrix, original_s_echo_noise};
}

#endif // ECHO_GENERATOR_H // 头文件结束宏