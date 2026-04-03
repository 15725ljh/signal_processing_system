// ============================================================================
//  JamTarDivi.h — 基于 Tsallis 交叉熵的干扰-目标信号时频解耦
//  Jamming-Target Signal Separation via Tsallis Cross-Entropy Thresholding
// ============================================================================
//
//  本文件为全系统统一的干扰解耦实现,以 03_jamming_detection_suppression 模块的
//  JamTarDivi.h 为蓝本提取至 common/ 目录,供所有模块共享使用.
//
//  算法原理:
//    将含干扰的混合信号通过 STFT 变换到时频域,在时频平面上利用 Tsallis 非广延
//    交叉熵进行二值分割(阈值法).交叉熵最小的灰度级即为最优分割阈值,低于该阈值
//    的时频能量判定为目标信号区域,高于阈值的判定为干扰信号区域.最后通过逆
//    STFT(列求和+权重归一化)分别重构干扰信号和目标信号的时域波形.
//
//    Tsallis 熵是 Boltzmann-Gibbs 熵的推广,参数 q 控制非广延性:
//      - q=1 时退化为经典 Shannon 熵
//      - q>1 时对弱信号(低灰度区域)更敏感,适合强干扰场景下的弱目标提取
//
//  与各模块旧版 JamTarDivi() 的关键差异:
//    1. 接口设计: 返回 JamTarDiviResult 结构体(而非 void+6个引用参数)
//    2. 安全检查: 空数据/零除/epsilon 保护/D 全为 max 的边界处理
//    3. STFT 实现: 使用 TfrStft::tfrstft() 统一版本(零填充+scale_factor)
//    4. 灰度量化: min_val 基准量化 + clamp 到 [0,255],防止溢出
//    5. 高阶谱判断: 使用 maxCoeff()(而非 mean()),对异常脉冲更敏感
//    6. 权重计算: colwise sum(沿时间轴求和,正确对应信号重构)
//
//  依赖:
//    - Eigen/Dense       矩阵运算
//    - Config.h          配置管理器(读取 detection_suppression.* 参数)
//    - TfrStft.h         STFT 统一实现
//
//  配置项:
//    detection_suppression.divi_stft_num          STFT 频点数,默认 256
//    detection_suppression.divi_hamming_len       STFT 汉明窗长度,默认 31
//    detection_suppression.tsallis_q              Tsallis 熵参数 q,默认 2.0
//    detection_suppression.gaojiepu_threshold      高阶谱判定阈值,默认 35.0
//
//  使用示例:
//    Eigen::VectorXcd mixed_signal = ...;                         // 含干扰的信号
//    JamTarDiviResult result = jamTarDivi(mixed_signal);           // 执行解耦
//    Eigen::VectorXcd jam = result.jammingsignal;                 // 提取的干扰信号
//    Eigen::VectorXcd target = result.targetsignal;               // 提取的目标信号
//    int flag = result.gaojiepu_idx;                              // 高阶谱标志(0/1)
//
// ============================================================================

#ifndef COMMON_JAMTARDIVI_H
#define COMMON_JAMTARDIVI_H

#include <Eigen/Dense>        // Eigen线性代数库(MatrixXcd, VectorXcd, VectorXd等)
#include "Config.h"           // 配置管理器(读取detection_suppression.*参数)
#include "TfrStft.h"          // STFT 统一实现(TfrStft::tfrstft)
#include <vector>             // STL动态数组(std::vector<double>)
#include <cmath>              // 数学函数(std::pow, std::cos, M_PI等)
#include <numeric>            // STL数值算法
#include <iostream>           // 标准输入输出
#include <limits>             // 数值极限(std::numeric_limits<double>::epsilon)

using namespace Eigen;

/**
 * @struct JamTarDiviResult
 * @brief  干扰解耦结果结构体,存储 jamTarDivi() 的输出
 *
 * 调用 jamTarDivi() 后返回此结构体,包含分离出的干扰信号、目标信号和
 * 高阶谱标志.所有信号均为复数向量,维度与输入信号相同.
 */
struct JamTarDiviResult {
    VectorXcd jammingsignal;   // 分离出的干扰信号(复数列向量,维度与输入data相同)
    VectorXcd targetsignal;    // 分离出的目标信号(复数列向量,维度与输入data相同)
    int gaojiepu_idx;          // 高阶谱标志: 0=正常脉冲, 1=判定为高阶谱异常脉冲
    int threshold;             // Tsallis交叉熵最优分割阈值(1-based灰度级)
};

/**
 * @brief 生成汉明窗(Hamming Window)
 *
 * 汉明窗是一种常用的余弦窗,主瓣宽度约为矩形窗的1.5倍,但第一旁瓣衰减
 * 约 -43dB,远优于矩形窗的 -13dB.本函数生成**未归一化**的汉明窗,
 * 窗函数系数满足: w(n) = 0.54 - 0.46 * cos(2*pi*n / (size-1))
 *
 * @param size  窗长度(正整数,如 31)
 * @return      汉明窗向量,维度为 size×1,元素值范围约 [0.08, 1.0]
 *
 * @note  本函数为 JamTarDivi 内部使用的辅助函数,未进行能量归一化.
 *        各模块 Module0.h 中的 generateHammingWindow() 提供归一化版本.
 */
inline VectorXd hamming_window_internal(int size) {
    VectorXd window(size);
    for (int i = 0; i < size; ++i) {
        window(i) = 0.54 - 0.46 * std::cos(2 * M_PI * i / (size - 1));
    }
    return window;
}

/**
 * @brief 基于 Tsallis 交叉熵的干扰-目标信号时频解耦
 *
 * 将含干扰的混合信号通过 STFT 变换到时频域,在时频平面上计算灰度直方图,
 * 然后遍历所有可能的分割阈值(0~255),对每个阈值计算 Tsallis 交叉熵.
 * 交叉熵最小的阈值即为最优分割点,低于该阈值的时频能量区域判定为目标信号,
 * 高于阈值的判定为干扰信号.最后通过列求和+权重归一化分别重构时域信号.
 *
 * 算法步骤:
 *   1. STFT: 将输入信号转换到时频域(使用 TfrStft::tfrstft)
 *   2. 权重计算: weight = mean(|colwise_sum(STFT)|) / mean(|data|)
 *   3. 幅度量化: 将 |STFT| 线性映射到 0~255 灰度级
 *   4. 灰度直方图: 统计每个灰度级的出现概率 p[i], i=0..255
 *   5. Tsallis 交叉熵: 对每个阈值 rgb,计算前景(C0)和背景(C1)的交叉熵 D0, D1
 *      总熵 D = D0 + D1 + D0*D1 (Tsallis 非广延性修正项)
 *   6. 最优阈值: argmin(D),对应的灰度级即为分割阈值 index
 *   7. 二值分割: 灰度值 < index 的时频单元标记为目标(0),>= index 标记为干扰(1)
 *   8. 信号重构: 干扰时频图的列求和 / weight = 干扰信号时域波形
 *   9. 目标信号 = 原始信号 - 干扰信号
 *  10. 高阶谱判定: 若 max(|targetsignal|) < threshold,标记为异常脉冲
 *
 * @param data  输入的混合信号(复数列向量,如单个脉冲的距离向采样,维度 nrn×1)
 * @return      JamTarDiviResult 结构体,包含:
 *              - jammingsignal: 分离出的干扰信号(与输入同维度)
 *              - targetsignal:  分离出的目标信号(与输入同维度)
 *              - gaojiepu_idx:  高阶谱标志(0=正常, 1=异常)
 */
inline JamTarDiviResult jamTarDivi(const VectorXcd& data) {

    // ---- 初始化结果结构体 ----
    JamTarDiviResult result;
    result.jammingsignal = VectorXcd::Zero(data.size());   // 干扰信号初始化为零向量
    result.targetsignal = VectorXcd::Zero(data.size());     // 目标信号初始化为零向量
    result.gaojiepu_idx = 0;                                // 高阶谱标志初始化为0(正常)
    result.threshold = 0;                                   // 阈值初始化为0

    // 安全检查: 输入数据为空时直接返回零结果
    if (data.size() == 0) {
        return result;
    }

    // ---- 第一步: 计算输入信号的 STFT 时频表示 ----
    // 04模块优先从processing节读取参数,兼容03模块的detection_suppression节
    const int STFT_NUM = Config::instance().getInt("processing.divi_stft_num",
        Config::instance().getInt("detection_suppression.divi_stft_num", 256));
    const int divi_hamming_len = Config::instance().getInt("processing.divi_hamming_len",
        Config::instance().getInt("detection_suppression.divi_hamming_len", 31));

    // 生成时间轴(1-based,与 MATLAB 索引习惯一致)
    VectorXd t_axis = VectorXd::LinSpaced(data.size(), 1, data.size());
    // 调用 TfrStft 统一实现,返回时频矩阵 tfr(维度: STFT_NUM × data.size())
    MatrixXcd tfr = TfrStft::tfrstft(data, t_axis, STFT_NUM, hamming_window_internal(divi_hamming_len));

    long N = tfr.rows();   // 时频图的频率维度(STFT_NUM,如 256)
    long M = tfr.cols();   // 时频图的时间维度(data.size(),如 nrn=2048)

    // 安全检查: STFT 结果为空时直接返回零结果
    if (N == 0 || M == 0) {
        return result;
    }

    // ---- 第二步: 计算信号重构的权重因子 ----
    // 对 STFT 时频图沿时间轴(colwise)求和,得到每个频率分量的总能量
    VectorXcd tfr_sum = tfr.colwise().sum();  // 维度: N×1

    // 权重 = STFT列和的平均幅度 / 原始信号的平均幅度
    // 该权重用于将时频域的干扰能量正确反投影回时域
    // (因为 STFT 的加窗和重叠会导致能量增益)
    double data_mean_abs = data.cwiseAbs().mean();          // 原始信号的平均幅度
    double tfr_sum_mean_abs = tfr_sum.cwiseAbs().mean();    // STFT 列和的平均幅度
    double weight = (data_mean_abs > std::numeric_limits<double>::epsilon()) ? (tfr_sum_mean_abs / data_mean_abs) : 1.0;

    // ---- 第三步: 计算幅度谱并自适应量化为 0~255 灰度级 ----
    MatrixXd abs_TFR = tfr.cwiseAbs();           // STFT 的幅度谱(实数矩阵)

    // 自适应量化下限: 使用幅度谱中位数 * 2 作为噪声基底阈值
    // 当过采样率较高(如3×B)时,TF平面大部分区域为空(噪声),
    // 若使用绝对最小值量化,噪声区域会占据大量低灰度级,
    // 导致Tsallis阈值在"噪声/信号"边界处分割而非"目标/干扰"边界
    // 使用中位数*2作为量化下限,可将噪声区域统一映射到灰度0,
    // 使目标/干扰的动态范围占据灰度1-255,提高分割精度
    long num = N * M;
    std::vector<double> all_abs(num);
    for (long i = 0; i < num; ++i) all_abs[i] = abs_TFR.data()[i];
    std::nth_element(all_abs.begin(), all_abs.begin() + num / 2, all_abs.end());
    double noise_floor = all_abs[num / 2];  // 中位数
    // 噪声基底倍数: 可从配置文件读取,默认3.0(高于中位数×3视为信号)
    double noise_multiplier = Config::instance().getDouble("processing.noise_floor_multiplier",
        Config::instance().getDouble("detection_suppression.noise_floor_multiplier", 3.0));
    double min_val = noise_floor * noise_multiplier;  // 噪声基底阈值

    double max_val = abs_TFR.maxCoeff();          // 幅度谱最大值
    // 量化步长: 将幅度范围 [min_val, max_val] 线性映射到 [0, 255] 的 256 个灰度级
    double delta_P = (max_val - min_val) / 255.0;
    // 防止除零: 若幅度范围极小(如信号全为零),设置 delta_P=1 避免量化异常
    if (delta_P < std::numeric_limits<double>::epsilon()) delta_P = 1.0;

    // 灰度量化: round((abs_TFR - min_val) / delta_P),然后 clamp 到 [0, 255]
    // 低于噪声基底的TF单元统一映射到灰度0
    Matrix<int, Dynamic, Dynamic> TFR_RGB = ((abs_TFR.array() - min_val) / delta_P).round().cast<int>();
    TFR_RGB = TFR_RGB.cwiseMax(0).cwiseMin(255);

    // ---- 第四步: 计算灰度直方图(概率分布,排除灰度0=噪声区域) ----

    // 统计每个灰度级的出现次数(仅统计灰度>0的信号区域)
    std::vector<double> p(256, 0.0);              // 256 个灰度级的概率数组
    long num_signal = 0;                          // 信号区域像素数(灰度>0)
    for (long i = 0; i < num; ++i) {
        int val = *(TFR_RGB.data() + i);          // 逐像素读取灰度值(行优先存储)
        if (val > 0 && val < 256) {
            p[val]++;                              // 仅统计信号区域灰度值
            num_signal++;
        }
    }
    // 归一化为概率(仅信号区域): p[i] = count[i] / num_signal
    long norm_count = (num_signal > 0) ? num_signal : num;
    for (int i = 1; i < 256; ++i) p[i] /= norm_count;
    p[0] = 0;  // 灰度0(噪声区域)不参与阈值计算

    // ---- 第五步: 计算 Tsallis 交叉熵 ----
    // Tsallis 熵参数 q: 控制非广延性,q>1 时对弱信号更敏感
    // 04模块优先从processing节读取,兼容03模块的detection_suppression节
    const double q = Config::instance().getDouble("processing.tsallis_q",
        Config::instance().getDouble("detection_suppression.tsallis_q", 2.0));

    // D 数组存储每个阈值对应的 Tsallis 交叉熵,初始值设为 max(即"无效"标记)
    // 遍历结束后 argmin(D) 即为最优分割阈值
    VectorXd D = VectorXd::Constant(256, std::numeric_limits<double>::max());

    // 遍历所有可能的分割阈值 rgb_1_based = 1..256 (对应 MATLAB 的 1-based 索引)
    // 阈值将灰度直方图分为两组: C0(灰度 0..rgb-1) 和 C1(灰度 rgb..255)
    for (int rgb_1_based = 1; rgb_1_based <= 256; ++rgb_1_based) {
        int rgb_0_based = rgb_1_based - 1;       // C++ 0-based 索引

        // 计算前景组 C0(灰度 0..rgb_1_based-1) 的总概率 W0
        // 和背景组 C1(灰度 rgb_1_based..255) 的总概率 W1 = 1 - W0
        double W0 = 0, W1 = 0;
        for(int i=0; i<rgb_1_based; ++i) W0 += p[i];
        W1 = 1.0 - W0;

        // 跳过退化情况: 若某组概率接近零,该阈值的熵无意义
        if (W0 < std::numeric_limits<double>::epsilon() || W1 < std::numeric_limits<double>::epsilon()) {
            continue;
        }

        // 计算 C0 的平均灰度 m0 = sum(i * p[i]) / W0 (i=0..rgb_1_based-1)
        double m0 = 0, m1 = 0;
        for (int i = 0; i < rgb_1_based; ++i) m0 += i * p[i];
        m0 /= W0;
        // 计算 C1 的平均灰度 m1 = sum(j * p[j]) / W1 (j=rgb_1_based..255)
        for (int j = rgb_1_based; j < 256; ++j) m1 += j * p[j];
        m1 /= W1;

        // 计算 C0 的 Tsallis 交叉熵分量 D0
        // 公式: D0 = sum_{i=0}^{rgb-1} p[i] * (m0 * (i/m0)^q - i)
        double D0 = 0, D1 = 0;
        if (m0 > std::numeric_limits<double>::epsilon()) {
            for (int i = 0; i < rgb_1_based; ++i) D0 += p[i] * (m0 * std::pow(i / m0, q) - i);
        }
        // 计算 C1 的 Tsallis 交叉熵分量 D1
        // 公式: D1 = sum_{j=rgb}^{255} p[j] * (m1 * (j/m1)^q - j)
        if (m1 > std::numeric_limits<double>::epsilon()) {
            for (int j = rgb_1_based; j < 256; ++j) D1 += p[j] * (m1 * std::pow(j / m1, q) - j);
        }

        // Tsallis 总交叉熵 = D0 + D1 + D0*D1
        // 其中 D0*D1 是 Tsallis 非广延修正项(Shannon 熵没有此项)
        D(rgb_0_based) = D0 + D1 + D0 * D1;
    }

    // ---- 第六步: 找到最优分割阈值 ----
    // 取交叉熵最小的灰度级作为分割阈值
    long index_0_based = 0;
    if (D.minCoeff() == std::numeric_limits<double>::max()) {
        // 边界情况: 所有阈值都被跳过(D 全为 max),使用默认阈值 0
        index_0_based = 0;
    } else {
        // 正常情况: 找到第一个等于最小值的索引(取第一个,不取最后一个)
        double min_D_val = D.minCoeff();
        for (int i = 0; i < D.size(); ++i) {
            if (D(i) == min_D_val) {
                index_0_based = i;
                break;
            }
        }
    }

    // 转换为 1-based 索引(与 MATLAB 阈值语义一致):
    // 灰度值 < index 的像素属于 C0(目标),灰度值 >= index 的像素属于 C1(干扰)
    long index = index_0_based + 1;
    result.threshold = static_cast<int>(index);

    // ---- 第七步: 根据阈值生成二值掩模并提取干扰信号 ----
    // 掩模: 灰度值 < index → 0(目标区域), 灰度值 >= index → 1(干扰区域)
    MatrixXd jam_tfr_mask = (TFR_RGB.array() < index).select(MatrixXd::Constant(N, M, 0.0), MatrixXd::Constant(N, M, 1.0));

    // 将掩模应用到原始 STFT: 保留干扰区域(掩模=1)的 STFT 值,目标区域(掩模=0)置零
    MatrixXcd JammingTFR = (jam_tfr_mask.array() == 1).select(tfr, MatrixXcd::Constant(N, M, 0.0));

    // ---- 第八步: 重构干扰信号的时域波形 ----
    // 对干扰时频图沿时间轴(colwise)求和,除以权重因子,转置为列向量
    // 这是 STFT 的一种简化逆变换(仅求和,不进行 IFFT),在窗函数满足
    // 完美重构条件时等价于 IFFT;实际中通过 weight 校正幅度误差
    result.jammingsignal = (JammingTFR.colwise().sum() / weight).transpose();

    // ---- 第九步: 计算目标信号 ----
    // 目标信号 = 原始混合信号 - 干扰信号(残余法)
    result.targetsignal = data - result.jammingsignal;

    // ---- 第十步: 高阶谱标志判定 ----
    // 若目标信号的最大幅度低于阈值,说明该脉冲可能被完全判定为干扰,
    // 目标信号残余极小,标记为高阶谱异常脉冲
    double gaojiepu_threshold = Config::instance().getDouble("processing.gaojiepu_threshold",
        Config::instance().getDouble("detection_suppression.gaojiepu_threshold", 35.0));
    result.gaojiepu_idx = (result.targetsignal.cwiseAbs().maxCoeff() < gaojiepu_threshold) ? 1 : 0;

    return result;
}

#endif // COMMON_JAMTARDIVI_H
