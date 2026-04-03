#ifndef GR_DETECTION_H // 防止头文件被重复包含
#define GR_DETECTION_H // 定义宏，表示头文件已被包含

#include <Eigen/Dense>
#include "Config.h"
#define CFG Config::instance()
#include "TfrStft.h"
#include "JamLocated.h"
#include <vector>
#include <cmath>
#include <numeric>
#include <fftw3.h>

using namespace Eigen; // 使用 Eigen 命名空间，简化代码

/**
 * @brief 辅助函数：生成汉明窗
 * @param size 窗的长度。
 * @return 生成的汉明窗向量。
 */
inline VectorXd hamming_window_gr(int size) {
    VectorXd window(size);
    for (int i = 0; i < size; ++i) {
        // 汉明窗公式: 0.54 - 0.46 * cos(2 * M_PI * i / (size - 1))
        window(i) = 0.54 - 0.46 * cos(2 * M_PI * i / (size - 1));
    }
    return window;
}

/**
 * @brief 辅助函数：对矩阵的第一维执行 fftshift 操作 (类似于 MATLAB 的 fftshift(matrix, 1))
 * @param mat 输入矩阵。
 * @return 经过 fftshift 处理后的矩阵。
 */
inline MatrixXd fftshift_dim1_gr(const MatrixXd& mat) {
    long N = mat.rows(); // 获取矩阵的行数
    long shift = floor(static_cast<double>(N) / 2.0); // 计算移位量
    MatrixXd shifted_mat(N, mat.cols()); // 创建一个与输入矩阵大小相同的新矩阵

    if (N > 0) {
        // 将下半部分移动到上半部分
        shifted_mat.topRows(N - shift) = mat.bottomRows(N - shift);
        // 将上半部分移动到下半部分
        shifted_mat.bottomRows(shift) = mat.topRows(shift);
    }
    return shifted_mat;
}

/**
 * @brief 干扰识别
 * 该函数通过对输入信号进行时频分析，并结合 Otsu 阈值分割和边缘检测，
 * 识别出不同类型的干扰信号。
 * @param B 带宽 (在此函数中未使用，但保留与 MATLAB 接口一致)。
 * @param Tp 脉宽 (在此函数中未使用，但保留与 MATLAB 接口一致)。
 * @param data 输入信号 (列向量)。
 * @return int 干扰类型 (0: 未识别或无干扰, 1: 间歇直接转发干扰 (ISDJ), 间歇重复转发干扰 (ISRJ), 或间歇循环转发干扰 (ISCJ) (统一归类), 2: 距离欺骗干扰 (RDJ), 3: 窄带瞄频干扰 (NBJ))。
 */
inline int grDetection(double B, double Tp, const VectorXcd& data) {
    (void)B; (void)Tp; // 抑制未使用的参数警告，表明这些参数是故意未使用的
    unsigned int J_type = 0; // 初始化干扰类型为 0 (未识别或无干扰)

    const int STFT_NUM = CFG.getInt("detection_suppression.gr_stft_num", 256);
    const int hamming_len = CFG.getInt("detection_suppression.gr_hamming_len", 63);

    if (data.size() == 0) {
        return J_type;
    }

    VectorXd t_axis = VectorXd::LinSpaced(data.size(), 1, data.size());
    MatrixXcd tfr = TfrStft::tfrstft(data, t_axis, STFT_NUM, hamming_window_gr(hamming_len));

    // 如果时频图为空，直接返回默认干扰类型
    if (tfr.size() == 0) {
        return J_type;
    }

    // 使用 Otsu 方法在时频图上定位干扰区域并执行 fftshift
    // MATLAB 行为: jam_tfr = JamLocated(tfr); jam_tfr = fftshift(jam_tfr, 1);
    MatrixXd jam_tfr = fftshift_dim1_gr(jamLocated(tfr));

    // 如果干扰时频图为空，直接返回默认干扰类型
    if (jam_tfr.size() == 0) {
        return J_type;
    }

    // 计算时频图每行的和，用于后续分析
    // MATLAB 行为: F_TEMP = sum(jam_tfr, 2);
    VectorXd F_TEMP = jam_tfr.rowwise().sum();

    // 如果 F_TEMP 为空，直接返回默认干扰类型
    if (F_TEMP.size() == 0) {
        return J_type;
    }

    // 计算阈值 miu1 并基于它进行二值化
    // MATLAB 行为: miu1 = mean(F_TEMP) * 0.3; A1 = F_TEMP > miu1;
    double miu1 = F_TEMP.mean() * 0.3;
    Array<bool, -1, 1> A1 = F_TEMP.array() > miu1;

    // 如果 A1 为空，直接返回默认干扰类型
    if (A1.size() == 0) {
        return J_type;
    }

    // 在二值化序列 A1 中查找上升沿和下降沿（用于识别脉冲边界）
    // MATLAB 行为: B1 = []; B2 = []; (查找上升沿和下降沿)
    std::vector<long> B1, B2; // B1 存储上升沿索引，B2 存储下降沿索引
    for (long j = 0; j < A1.size() - 1; ++j) {
        if (!A1(j) && A1(j + 1)) B1.push_back(j + 1); // 0 -> 1 (上升沿)
        if (A1(j) && !A1(j + 1)) B2.push_back(j);     // 1 -> 0 (下降沿)
    }
    // 处理边界情况: 如果序列以 1 开始，则假定在 0 处有一个上升沿
    if (A1(0)) B1.insert(B1.begin(), 0);
    // 处理边界情况: 如果序列以 1 结束，则假定在末尾有一个下降沿
    if (A1.tail(1)(0)) B2.push_back(A1.size() - 1);

    // 如果 B1 或 B2 为空，直接返回默认干扰类型
    if (B1.empty() || B2.empty()) {
        return J_type;
    }

    // 根据上升沿的数量确定干扰类型
    // MATLAB 行为: if length(B1) > 1, J_type = 1; end
    if (B1.size() > 1) {
        J_type = 1; // 如果有多个上升沿，分类为 ISDJ, ISRJ, 或 ISCJ (统一归类)
    } else if (B1.size() == 1) {
        long index1 = B1[0] + 2; // 调整索引
        long index2 = B2[0] - 2; // 调整索引
        // 确保索引在有效范围内
        if (index1 >= STFT_NUM) index1 = STFT_NUM - 1;
        if (index2 < 0) index2 = 0;

        // 确保调整后的索引有效，防止越界访问
        if (index1 >= jam_tfr.rows() || index2 >= jam_tfr.rows() || index1 < 0 || index2 < 0) {
            return J_type;
        }

        if (index1 <= index2) {
            RowVectorXd C1 = jam_tfr.row(index1); // 从时频图中获取一行
            std::vector<long> D1; // 存储 C1 中非零段的起始索引
            for (long j = 0; j < C1.size() - 1; ++j) {
                if (C1(j) == 0 && C1(j + 1) != 0) D1.push_back(j); // 查找 0 -> 非 0 的转换
            }

            if (D1.size() == 1) { // 如果只有一个非零段
                // 确保 data_ABS 不为空
                if (data.cwiseAbs().size() == 0) {
                    return J_type;
                }
                VectorXd data_ABS = data.cwiseAbs(); // 信号的绝对值
                double mid_level = (data_ABS.maxCoeff() + data_ABS.cwiseAbs().minCoeff()) / 2.0; // 计算中位数
                Array<bool, -1, 1> E1 = data_ABS.array() > mid_level; // 基于中位数进行二值化
                std::vector<long> E3; // 存储 E1 中的上升沿索引
                for (long j = 0; j < E1.size() - 1; ++j) {
                    if (!E1(j) && E1(j+1)) E3.push_back(j); // 查找 0 -> 1 的转换
                }
                if (E3.size() == 1) J_type = 2; // 如果只有一个上升沿，分类为 RDJ
            } else {
                 J_type = 3; // 对应 MATLAB 的 if length(D1) ~= 1 分支，分类为 NBJ
            }
        }
    }
    return J_type; // 返回识别到的干扰类型
}

#endif // GR_DETECTION_H // 头文件结束宏