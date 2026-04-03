#ifndef JAM_TAR_DIVI_H // 防止头文件被重复包含
#define JAM_TAR_DIVI_H // 定义宏，表示头文件已被包含

#include <Eigen/Dense>
#include "Config.h"
#define CFG Config::instance()
#include "TfrStft.h"
#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>
#include <limits>
#include <fftw3.h>

using namespace Eigen; // 使用 Eigen 命名空间，简化代码

/**
 * @brief 存储干扰信号和目标信号分解结果的结构体
 * 包含分解后的干扰信号、目标信号以及一个高阶谱索引。
 */
struct JamTarDiviResult {
    VectorXcd jammingsignal; // 分解出的干扰信号
    VectorXcd targetsignal;  // 分解出的目标信号
    int gaojiepu_idx;        // 高阶谱索引，用于指示目标信号的特性
};

/**
 * @brief 辅助函数：生成汉明窗
 * @param size 窗的长度。
 * @return 生成的汉明窗向量。
 */
inline VectorXd hamming_window_internal(int size) {
    VectorXd window(size);
    for (int i = 0; i < size; ++i) {
        window(i) = 0.54 - 0.46 * std::cos(2 * M_PI * i / (size - 1));
    }
    return window;
}

/**
 * @brief 将信号分解为干扰信号和目标信号
 * 该函数通过时频分析和基于 Tsallis 交叉熵的阈值分割方法，
 * 将输入的混合信号分解为干扰信号和目标信号。
 * @param data 输入的混合信号 (复数列向量)。
 * @return JamTarDiviResult 结构体，包含分解后的干扰信号、目标信号和高阶谱索引。
 */
inline JamTarDiviResult jamTarDivi(const VectorXcd& data) {

    JamTarDiviResult result;
    result.jammingsignal = VectorXcd::Zero(data.size()); // 初始化干扰信号为零向量，避免潜在问题
    result.targetsignal = VectorXcd::Zero(data.size());  // 初始化目标信号为零向量，避免潜在问题
    result.gaojiepu_idx = 0;

    if (data.size() == 0) { // 安全检查: 确保输入数据不为空
        return result;
    }

    // MATLAB 等效操作: STFT_NUM = 256;
    const int STFT_NUM = CFG.getInt("detection_suppression.divi_stft_num", 256);
    const int divi_hamming_len = CFG.getInt("detection_suppression.divi_hamming_len", 31);

    VectorXd t_axis = VectorXd::LinSpaced(data.size(), 1, data.size());
    MatrixXcd tfr = TfrStft::tfrstft(data, t_axis, STFT_NUM, hamming_window_internal(divi_hamming_len));


    // MATLAB 等效操作: [N, M] = size(tfr);
    long N = tfr.rows(); // 时频图的行数 (频率维度)
    long M = tfr.cols(); // 时频图的列数 (时间维度)

    if (N == 0 || M == 0) {
        return result;
    }

    // MATLAB 等效操作: tfr_sum = sum(tfr);
    VectorXcd tfr_sum = tfr.colwise().sum(); // 计算时频图每列的和

    // MATLAB 等效操作: weight = mean(abs(tfr_sum)) / mean(abs(data));
    double data_mean_abs = data.cwiseAbs().mean(); // 输入信号绝对值的平均值
    double tfr_sum_mean_abs = tfr_sum.cwiseAbs().mean(); // 时频图列和绝对值的平均值
    // 计算权重因子，用于归一化。防止除以零，如果 data_mean_abs 接近于零，则权重设为 1.0
    double weight = (data_mean_abs > std::numeric_limits<double>::epsilon()) ? (tfr_sum_mean_abs / data_mean_abs) : 1.0;

    // MATLAB 等效操作: abs_TFR = abs(tfr);
    MatrixXd abs_TFR = tfr.cwiseAbs(); // 计算时频图的幅度

    // MATLAB 等效操作: delta_P = (max(max(abs_TFR)) - min(min(abs_TFR))) / 255;
    double min_val = abs_TFR.minCoeff(); // 幅度图中的最小值
    double max_val = abs_TFR.maxCoeff(); // 幅度图中的最大值
    double delta_P = (max_val - min_val) / 255.0; // 每个灰度级对应的幅度差
    // 防止除零错误，如果幅度范围极小，则将 delta_P 设为 1.0
    if (delta_P < std::numeric_limits<double>::epsilon()) delta_P = 1.0;

    // MATLAB 等效操作: TFR_RGB = round(abs_TFR ./ delta_P);
    // 将幅度图转换为 256 级灰度图像
    Matrix<int, Dynamic, Dynamic> TFR_RGB = ((abs_TFR.array() - min_val) / delta_P).round().cast<int>();
    // 确保灰度值在 0 到 255 之间
    TFR_RGB = TFR_RGB.cwiseMax(0).cwiseMin(255);

    // MATLAB 等效操作: num = N * M;
    long num = N * M; // 灰度图像的总像素数量

    // MATLAB 等效操作: p = zeros(1, 256);
    std::vector<double> p(256, 0.0); // 初始化直方图，256 个灰度级，初始频率为 0
    for (long i = 0; i < num; ++i) {
        int val = *(TFR_RGB.data() + i);
        if (val >= 0 && val < 256) {
            p[val]++; // 统计每个灰度级出现的次数
        } else {
            // 可以在这里添加错误处理或日志记录，如果灰度值超出预期范围
        }
    }
    for (int i = 0; i < 256; ++i) p[i] /= num; // 归一化直方图，得到频率

    // MATLAB 等效操作: q = 2;
    const double q = CFG.getDouble("detection_suppression.tsallis_q", 2.0);

    // MATLAB 等效操作: D = zeros(1, 256);
    // 初始化 D 向量，用于存储每个阈值对应的 Tsallis 交叉熵，初始值为最大双精度浮点数
    VectorXd D = VectorXd::Constant(256, std::numeric_limits<double>::max());

    // MATLAB 等效操作: for rgb = 1:256
    // 遍历所有可能的阈值 (rgb_1_based 从 1 到 256，对应灰度值 0 到 255)
    for (int rgb_1_based = 1; rgb_1_based <= 256; ++rgb_1_based) {
        int rgb_0_based = rgb_1_based - 1; // 0-based 灰度值
        double W0 = 0, W1 = 0; // W0: 前景概率，W1: 背景概率
        for(int i=0; i<rgb_1_based; ++i) W0 += p[i]; // 计算前景累积概率
        W1 = 1.0 - W0; // 计算背景概率

        // 如果前景或背景概率接近于零，则跳过当前阈值，避免除零错误
        if (W0 < std::numeric_limits<double>::epsilon() || W1 < std::numeric_limits<double>::epsilon()) {
            continue;
        }

        double m0 = 0, m1 = 0; // m0: 前景平均灰度值，m1: 背景平均灰度值
        for (int i = 0; i < rgb_1_based; ++i) m0 += i * p[i]; // 计算前景加权平均灰度值
        m0 /= W0; // 归一化
        for (int j = rgb_1_based; j < 256; ++j) m1 += j * p[j]; // 计算背景加权平均灰度值
        m1 /= W1; // 归一化

        double D0 = 0, D1 = 0; // D0: 前景的 Tsallis 交叉熵，D1: 背景的 Tsallis 交叉熵
        if (m0 > std::numeric_limits<double>::epsilon()) {
            for (int i = 0; i < rgb_1_based; ++i) D0 += p[i] * (m0 * std::pow(i / m0, q) - i); // 计算前景 Tsallis 交叉熵
        }
        if (m1 > std::numeric_limits<double>::epsilon()) {
            for (int j = rgb_1_based; j < 256; ++j) D1 += p[j] * (m1 * std::pow(j / m1, q) - j); // 计算背景 Tsallis 交叉熵
        }
        D(rgb_0_based) = D0 + D1 + D0 * D1; // 计算总的 Tsallis 交叉熵
    }

    // 找到使 Tsallis 交叉熵最小的灰度级 (分割阈值)
    long index_0_based = 0;
    // 处理 D 可能全部是最大双精度值的情况 (如果所有 W0/W1 都太小)
    if (D.minCoeff() == std::numeric_limits<double>::max()) {
        index_0_based = 0; // 默认设置为 0-based 索引 0，即 1-based 索引 1
    } else {
        double min_D_val = D.minCoeff(); // 找到 D 中的最小值
        for (int i = 0; i < D.size(); ++i) {
            if (D(i) == min_D_val) {
                index_0_based = i; // 找到第一个匹配最小值的索引
                break; // 取第一个匹配项
            }
        }
    }

    long index = index_0_based + 1; // 将 C++ 0-based 索引转换为类似 MATLAB 的 1-based 索引

    // MATLAB 等效操作: jam_tfr = ones(N, M);
    // 根据阈值生成干扰掩码: 灰度值小于 index 的像素设置为 0 (目标区域)
    // 在 C++ 中，TFR_RGB 值的范围是 0-255。Index 的范围是 1-256。
    // 如果 index 是 1，TFR_RGB < 1 意味着只有 TFR_RGB == 0 是目标区域。
    // 如果 index 是 256，TFR_RGB < 256 意味着所有 TFR_RGB (0-255) 都是目标区域。
    MatrixXd jam_tfr_mask = (TFR_RGB.array() < index).select(MatrixXd::Constant(N, M, 0.0), MatrixXd::Constant(N, M, 1.0));

    // MATLAB 等效操作: JammingTFR(jam_tfr == 1) = tfr(jam_tfr == 1);
    // 根据掩码提取干扰时频图: 掩码为 1 的区域保留原始时频图值，否则为 0
    MatrixXcd JammingTFR = (jam_tfr_mask.array() == 1).select(tfr, MatrixXcd::Constant(N, M, 0.0));

    // MATLAB 等效操作: jammingsignal = (sum(JammingTFR) / weight).';
    // 计算干扰信号: 对干扰时频图按列求和，除以权重，并转置为列向量
    result.jammingsignal = (JammingTFR.colwise().sum() / weight).transpose();

    // MATLAB 等效操作: Targetsignal = data - jammingsignal;
    // 计算目标信号: 从原始信号中减去干扰信号
    result.targetsignal = data - result.jammingsignal;

    // MATLAB 等效操作: if abs(Targetsignal) < 35, gaojiepu_idx = 1; else, gaojiepu_idx = 0; end
    // 根据目标信号的绝对值最大值判断高阶谱索引
    double gaojiepu_threshold = CFG.getDouble("detection_suppression.gaojiepu_threshold", 35.0);
    result.gaojiepu_idx = (result.targetsignal.cwiseAbs().maxCoeff() < gaojiepu_threshold) ? 1 : 0;

    return result;
}

#endif // JAM_TAR_DIVI_H // 头文件结束宏