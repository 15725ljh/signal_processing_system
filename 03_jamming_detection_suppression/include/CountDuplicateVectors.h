#ifndef COUNT_DUPLICATE_VECTORS_H
#define COUNT_DUPLICATE_VECTORS_H

#include <Eigen/Dense>     // 包含 Eigen 库，用于矩阵和向量操作，如 MatrixXi, VectorXi, RowVectorXcd 等
#include <iostream>        // 包含标准输入输出流，用于控制台打印等
#include <ostream>         // 包含 ostream，用于日志输出流
#include <map>             // 包含 std::map 容器，用于存储唯一向量及其计数
#include <vector>          // 包含 std::vector 容器，用于动态数组
#include <algorithm>       // 包含 std::sort 和 std::lexicographical_compare 算法
#include <string>          // 包含 std::string 类型，用于字符串操作

using namespace Eigen; // 使用 Eigen 命名空间，简化代码

/**
 * @brief 存储向量计数结果的结构体
 * 包含唯一的向量集合和每个唯一向量的出现次数。
 */
struct VectorCountResult {
    MatrixXi uniqueVectors; // 存储所有唯一的向量，每行一个向量
    VectorXi counts;        // 存储每个唯一向量对应的出现次数
};

/**
 * @brief 辅助比较器结构体，允许 Eigen::VectorXi 作为 std::map 的键
 * std::map 要求其键是可排序的；Eigen::VectorXi 默认不支持，因此需要自定义比较规则。
 */
struct VectorComp {
    /**
     * @brief 重载函数调用运算符，用于比较两个 Eigen::VectorXi 向量
     * @param a 第一个向量
     * @param b 第二个向量
     * @return 如果 a 小于 b，则返回 true；否则返回 false。
     */
    bool operator()(const VectorXi& a, const VectorXi& b) const {
        // 使用字典序比较两个 Eigen::VectorXi 向量
        // 确保比较规则是严格弱序，以满足 std::map 的要求
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    }
};

/**
 * @brief 统计矩阵中相同向量的数量
 * 该函数根据指定的维度（行或列）统计输入矩阵中重复向量的出现次数，
 * 并返回唯一的向量及其对应的计数。
 * @param matrix_in 输入矩阵。
 * @param dim 统计维度 (1: 列向量, 2: 行向量)。
 * @param logStream 用于输出日志的 ostream 对象。
 * @return VectorCountResult 结构体，包含唯一的向量矩阵和它们的计数向量。
 */
inline VectorCountResult countDuplicateVectors(const MatrixXi& matrix_in, int dim, std::ostream& logStream) {
    // MATLAB 行为: 如果未指定 dim，则默认为 2 (按行计数)。在 C++ 中，这通过函数默认参数处理。
    MatrixXi matrix = matrix_in; // 创建输入矩阵的副本，用于转置操作

    // 根据计数维度进行转置：如果按列向量计数 (dim == 1)，则将矩阵原地转置，
    // 使列向量变为行向量，便于后续按行处理。
    if (dim == 1) {
        matrix.transposeInPlace(); // 原地转置矩阵
    }

    // 使用 std::map 统计唯一向量及其出现次数
    // map 的键是唯一的向量 (VectorXi)，值是该向量出现的次数 (int)
    // VectorComp 是用于比较 VectorXi 类型键的自定义比较器
    std::map<VectorXi, int, VectorComp> counts_map;
    for (long i = 0; i < matrix.rows(); ++i) {
        // 遍历矩阵的每一行（现在每一行代表一个要计数的向量）
        // 使用当前行向量作为 map 的键，并增加其对应的计数
        counts_map[matrix.row(i)]++;
    }

    // 将 map 的结果（键值对）转换为 std::vector，以便进行排序
    std::vector<std::pair<VectorXi, int>> sorted_counts;
    for (const auto& pair : counts_map) {
        sorted_counts.push_back(pair);
    }

    // 排序计数结果：按向量的出现次数降序排序
    // MATLAB 行为: [counts, sortIdx] = sort(counts, 'descend');
    std::sort(sorted_counts.begin(), sorted_counts.end(),
        [](const std::pair<VectorXi, int>& a, const std::pair<VectorXi, int>& b) {
            // 匿名函数作为比较准则：如果 a 的计数大于 b 的计数，则 a 排在 b 之前
            return a.second > b.second; // 按计数降序排序
        }
    );

    // 填充返回结构体 VectorCountResult
    VectorCountResult result;
    if (!sorted_counts.empty()) {
        long num_unique = sorted_counts.size();     // 唯一向量的数量
        long vector_len = sorted_counts[0].first.size(); // 每个向量的长度（维度）

        // 调整结果结构体中 uniqueVectors 和 counts 的大小
        result.uniqueVectors.resize(num_unique, vector_len);
        result.counts.resize(num_unique);

        // 将排序后的唯一向量及其计数填充到结果结构体中
        for (long i = 0; i < num_unique; ++i) {
            result.uniqueVectors.row(i) = sorted_counts[i].first; // 复制唯一向量
            result.counts(i) = sorted_counts[i].second;           // 复制计数
        }
    }

    for (long i = 0; i < result.uniqueVectors.rows(); ++i) {
        int val = result.uniqueVectors(i, 0);
        logStream << "  类型" << val << ": " << result.counts(i) << "/" << result.counts.sum() << " (" << (100.0 * result.counts(i) / result.counts.sum()) << "%)";
        if (i < result.uniqueVectors.rows() - 1) logStream << "  |";
    }
    logStream << std::endl;

    return result; // 返回计数结果
}


#endif // COUNT_DUPLICATE_VECTORS_H
