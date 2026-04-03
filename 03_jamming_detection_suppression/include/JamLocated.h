#ifndef JAM_LOCATED_H // 防止头文件被重复包含
#define JAM_LOCATED_H // 定义宏，表示头文件已被包含

#include <Eigen/Dense>   // 包含 Eigen 库，用于矩阵和向量操作
#include <vector>        // 包含 std::vector 容器，用于存储动态大小的序列
#include <cmath>         // 包含数学函数，如 floor, pow，用于数值计算
#include <numeric>       // 包含数值操作，如 std::accumulate (尽管在此文件中未直接使用，但通常用于数值处理)
#include <algorithm>     // 包含 std::max_element，用于查找序列中的最大值

using namespace Eigen; // 使用 Eigen 命名空间，简化代码

/**
 * @brief 使用 Otsu 方法在时频图上定位干扰区域
 * 该函数实现了基于 Otsu 阈值分割算法的干扰定位功能。它首先将输入的复数时频图
 * 转换为其幅度的灰度图像，然后计算该灰度图像的最佳阈值，最后根据该阈值
 * 将时频图中的干扰区域（高于阈值的部分）标识出来。
 * @param tfr 输入的复数时频图 (MatrixXcd)，通常是 STFT 的结果。
 * @return 一个与输入时频图大小相同的 MatrixXd，其中干扰区域的值为1，非干扰区域的值为0。
 */
inline MatrixXd jamLocated(const MatrixXcd& tfr) {
    // 1. 计算时频图的幅度 (abs_TFR)
    // MATLAB 对应操作: abs_TFR = abs(tfr);
    MatrixXd abs_TFR = tfr.cwiseAbs();

    // 2. 计算灰度转换的比例因子 (delta_P)
    // 将幅度值归一化到 0-255 的灰度范围，delta_P 是每个灰度级对应的幅度值范围。
    // MATLAB 对应操作: delta_P = (max(max(abs_TFR)) - min(min(abs_TFR))) / 255;
    double min_val = abs_TFR.minCoeff(); // 获取幅度图中的最小值
    double max_val = abs_TFR.maxCoeff(); // 获取幅度图中的最大值
    double delta_P = (max_val - min_val) / 255.0; // 计算每个灰度级对应的幅度差
    // 防止除零错误，如果幅度范围极小，则将 delta_P 设为 1.0，避免后续计算出现 NaN 或 Inf
    if (delta_P < 1e-9) delta_P = 1.0;

    // 3. 将幅度图转换为 256 级灰度图像 (TFR_RGB)
    // MATLAB 对应操作: TFR_RGB = round(abs_TFR ./ delta_P);
    // 先将幅度值减去最小值，然后除以 delta_P 进行归一化，四舍五入后转换为整数类型。
    Matrix<int, Dynamic, Dynamic> TFR_RGB = ((abs_TFR.array() - min_val) / delta_P).round().cast<int>();
    // 确保灰度值在 0 到 255 之间，超出范围的进行裁剪
    TFR_RGB = TFR_RGB.cwiseMax(0).cwiseMin(255);

    // 4. 计算灰度直方图 (p)
    // p 存储每个灰度级 (0-255) 出现的频率。
    // MATLAB 对应操作: num = size(TFR_RGB, 1) * size(TFR_RGB, 2); p = zeros(1, 256);
    long num = TFR_RGB.size(); // 获取灰度图像的总像素数量
    std::vector<double> p(256, 0.0); // 初始化直方图，256 个灰度级，初始频率为 0
    // 统计每个灰度级出现的次数
    for (long i = 0; i < num; ++i) {
        p[*(TFR_RGB.data() + i)]++;
    }
    // 将次数转换为频率 (归一化直方图)
    for (int i = 0; i < 256; ++i) {
        p[i] /= num;
    }

    // 5. 使用 Otsu 方法计算最佳阈值
    // Otsu 方法通过最大化类间方差来找到最佳阈值。
    // MATLAB 对应操作: vara = zeros(1, 256); for th = 1:256 ...
    std::vector<double> vara(256, 0.0); // 存储每个阈值对应的类间方差

    // 遍历所有可能的阈值 (th 从 1 到 256，对应灰度值 0 到 255)
    for (int th = 1; th <= 256; ++th) {
        // 将像素分为两类：C1 (灰度值 [0, th-1]) 和 C2 (灰度值 [th, 255])
        double p1 = 0, p2 = 0; // p1: C1 类像素的概率，p2: C2 类像素的概率
        // MATLAB 对应操作: p1 = sum(p(1:th));
        for(int i=0; i<th; ++i) p1 += p[i]; // 计算 C1 类像素的累积概率
        p2 = 1.0 - p1; // 计算 C2 类像素的概率

        // 如果某一类像素的概率接近于零，则类间方差为 0，避免除零错误
        if (p1 < 1e-9 || p2 < 1e-9) {
            vara[th-1] = 0;
            continue;
        }

        double m1 = 0, m2 = 0; // m1: C1 类像素的平均灰度值，m2: C2 类像素的平均灰度值
        // MATLAB 对应操作: m1 = 0; for j = 1:th; m1 = m1 + (j - 1) * p(j); end; m1 = m1 / p1;
        for (int j = 1; j <= th; ++j) m1 += (j - 1) * p[j-1]; // 计算 C1 类像素的加权平均灰度值
        m1 /= p1; // 归一化得到 C1 类平均灰度值

        // MATLAB 对应操作: m2 = 0; for j = th+1:256; m2 = m2 + (j - 1) * p[j-1]; end; m2 = m2 / p2;
        for (int j = th + 1; j <= 256; ++j) m2 += (j - 1) * p[j-1]; // 计算 C2 类像素的加权平均灰度值
        m2 /= p2; // 归一化得到 C2 类平均灰度值

        // 计算当前阈值下的类间方差
        // MATLAB 对应操作: vara(th) = p1 * p2 * (m1 - m2)^2;
        vara[th-1] = p1 * p2 * (m1 - m2) * (m1 - m2);
    }

    // 6. 找到最大类间方差对应的阈值 (idx_1based)
    // MATLAB 对应操作: [~, idx] = max(vara);
    auto max_it = std::max_element(vara.begin(), vara.end()); // 找到 vara 中最大值的迭代器
    long idx_1based = std::distance(vara.begin(), max_it) + 1; // 计算最大值对应的 1-based 索引 (即最佳阈值)

    // 7. 经验性调整最佳阈值 (index)
    // 经验性调整，将最佳阈值减去 10，以更好地适应干扰区域的识别。
    // MATLAB 对应操作: index = idx - 10;
    long index = idx_1based - 10;

    // 8. 确保调整后的阈值在有效范围内 [0, 255]
    // MATLAB 对应操作: index = max(min(index, 255), 0);
    index = std::max(0L, std::min(255L, index));

    // 9. 根据阈值生成干扰定位图 (jam_tfr)
    // 初始化一个全 1 的矩阵，与输入时频图大小相同
    // MATLAB 对应操作: jam_tfr = ones(size(tfr));
    MatrixXd jam_tfr = MatrixXd::Ones(tfr.rows(), tfr.cols());
    // 遍历灰度图像的每个像素
    // MATLAB 对应操作: for m = 1:size(tfr, 1) for n = 1:size(tfr, 2) if TFR_RGB(m, n) < index jam_tfr(m, n) = 0; end; end; end;
    for (long r = 0; r < tfr.rows(); ++r) {
        for (long c = 0; c < tfr.cols(); ++c) {
            // 如果像素的灰度值低于最佳阈值，则将其在干扰定位图中设为 0 (表示非干扰区域)
            if (TFR_RGB(r, c) < index) {
                jam_tfr(r, c) = 0;
            }
        }
    }

    // 10. 返回最终的干扰定位图
    return jam_tfr;
}

#endif // JAM_LOCATED_H // 头文件结束宏