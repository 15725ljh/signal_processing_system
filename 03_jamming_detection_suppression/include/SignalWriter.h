#ifndef SIGNAL_WRITER_H // 防止头文件被重复包含
#define SIGNAL_WRITER_H // 定义宏，表示头文件已被包含

#include <string>      // 包含 std::string 类型，用于字符串操作
#include <ostream>     // 包含 ostream，用于日志输出流
#include <Eigen/Dense> // 包含 Eigen 库，用于矩阵和向量操作，如 Eigen::VectorXcd 和 Eigen::RowVectorXcd
#include <fstream>     // 包含 fstream，用于文件输入输出流，如 std::ofstream
#include <iostream>    // 包含 iostream，用于标准输入输出流 (尽管 ostream 更具体，但通常也包含 iostream)
#include <filesystem>  // C++17 文件系统

// Windows UTF-8 路径支持: 解决中文文件名在 GBK 系统下乱码的问题
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace _sw_detail {
inline std::filesystem::path _u8path(const std::string& s) {
    if (s.empty()) return std::filesystem::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return std::filesystem::path(std::move(w));
}
}
#define SW_U8PATH(s) _sw_detail::_u8path(s)
#else
#define SW_U8PATH(s) (s)
#endif

/**
 * @brief 辅助函数：将 Eigen 复数向量写入文本文件
 * 将一个 Eigen 库的复数向量的实部和虚部分别写入到指定的文本文件中，每行一对实部和虚部。
 * @param vec 要写入的 Eigen 复数向量。
 * @param filename 目标文件的名称。
 * @param logStream 用于输出日志的 ostream 对象。
 */
inline void writeComplexVectorToFile(const Eigen::VectorXcd& vec, const std::string& filename, std::ostream& logStream) {
    std::ofstream outFile(SW_U8PATH(filename));
    if (outFile.is_open()) {
        for (int i = 0; i < vec.size(); ++i) {
            outFile << vec(i).real() << " " << vec(i).imag() << std::endl;
        }
        outFile.close();
        logStream << "文件已成功保存: " << filename << std::endl;
    } else {
        logStream << "错误：无法打开文件进行写入: " << filename << std::endl;
    }
}

/**
 * @brief 辅助函数：将 Eigen 复数行向量写入文本文件
 * 将一个 Eigen 库的复数行向量的实部和虚部分别写入到指定的文本文件中，每行一对实部和虚部。
 * @param vec 要写入的 Eigen 复数行向量。
 * @param filename 目标文件的名称。
 * @param logStream 用于输出日志的 ostream 对象。
 */
inline void writeComplexRowVectorToFile(const Eigen::RowVectorXcd& vec, const std::string& filename, std::ostream& logStream) {
    std::ofstream outFile(SW_U8PATH(filename));
    if (outFile.is_open()) {
        for (int i = 0; i < vec.size(); ++i) {
            outFile << vec(i).real() << " " << vec(i).imag() << std::endl;
        }
        outFile.close();
        logStream << "文件已成功保存: " << filename << std::endl;
    } else {
        logStream << "错误：无法打开文件进行写入: " << filename << std::endl;
    }
}

/**
 * @brief 新增辅助函数：将多个复数信号合并写入单个文件
 * 将四个复数信号 (s_echo_noise, Echo_CPI1, jammingsignal, Targetsignal) 的实部和虚部
 * 按列合并写入到指定的文本文件中。每行包含 8 列数据 (s_echo_noise_real, s_echo_noise_imag, ...)。
 * @param s_echo_noise 原始带噪回波信号。
 * @param Echo_CPI1 单个 CPI 的混合回波信号。
 * @param jammingsignal 分解出的干扰信号。
 * @param Targetsignal 分解出的目标信号。
 * @param filename 目标文件的名称。
 * @param logStream 用于输出日志的 ostream 对象。
 */
inline void writeConsolidatedSignalsToFile(
    const Eigen::RowVectorXcd& s_echo_noise,
    const Eigen::RowVectorXcd& Echo_CPI1,
    const Eigen::VectorXcd& jammingsignal,
    const Eigen::VectorXcd& Targetsignal,
    const std::string& filename, std::ostream& logStream) {

    std::ofstream outFile(SW_U8PATH(filename));
    if (!outFile.is_open()) {
        logStream << "错误：无法打开文件进行写入: " << filename << std::endl;
        return;
    }

    // 写入所有四个信号的数据，每行 8 列 (s_echo_noise_real s_echo_noise_imag Echo_CPI1_real Echo_CPI1_imag ...)
    // 假设所有信号长度相同，以 s_echo_noise 的长度为参考
    long signal_length = s_echo_noise.size();

    for (long i = 0; i < signal_length; ++i) {
        outFile << s_echo_noise(i).real() << " " << s_echo_noise(i).imag() << " "
                << Echo_CPI1(i).real() << " " << Echo_CPI1(i).imag() << " "
                << jammingsignal(i).real() << " " << jammingsignal(i).imag() << " "
                << Targetsignal(i).real() << " " << Targetsignal(i).imag() << "\n";
    }

    outFile.close();
    logStream << "合并信号文件已成功保存: " << filename << std::endl;
}

#endif // SIGNAL_WRITER_H // 头文件结束宏
