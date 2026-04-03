#ifndef TFRSTFT_H // 防止头文件被重复包含
#define TFRSTFT_H // 定义宏，表示头文件已被包含

#include <Eigen/Dense>
#include "Config.h"
#define CFG Config::instance()
#include <vector>
#include <complex>
#include <cmath>
#include <fftw3.h>

namespace TfrStft { // 定义 TfrStft 命名空间，用于封装时频分析相关函数

/**
 * @brief 执行短时傅里叶变换 (STFT)
 * 该函数计算输入信号 x 的短时傅里叶变换，生成其时频表示。
 * @param x 输入的复数信号向量。
 * @param t 时间轴向量，表示 STFT 计算的时间点。
 * @param N FFT 点数，决定频率分辨率。
 * @param h 窗函数向量，用于对信号进行加窗处理。
 * @return 信号的时频表示矩阵 (MatrixXcd)，其中行代表频率，列代表时间。
 */
inline Eigen::MatrixXcd tfrstft(const Eigen::VectorXcd& x, const Eigen::VectorXd& t, int N, const Eigen::VectorXd& h) {
    // 获取维度信息
    long xrow = x.size(); // 输入信号的长度
    long tcol = t.size(); // 时间轴的长度 (STFT 的时间点数量)
    long hrow = h.size(); // 窗函数的长度
    long Lh = (hrow - 1) / 2; // 窗函数半长

    // 初始化 tfr 矩阵，用于存储 STFT 结果，大小为 N (频率点数) x tcol (时间点数)
    Eigen::MatrixXcd tfr = Eigen::MatrixXcd::Zero(N, tcol);

    // x 和 h 的零填充
    // 为输入信号 x 创建一个零填充的向量，以便进行卷积操作
    Eigen::VectorXcd padded_x(xrow + 2 * Lh);
    for (long i = 0; i < Lh; ++i) {
        padded_x(i) = 0.0; // 最初的零填充
    }
    for (long i = 0; i < xrow; ++i) {
        padded_x(Lh + i) = x(i); // 原始信号 x
    }
    for (long i = 0; i < Lh; ++i) {
        padded_x(Lh + xrow + i) = 0.0; // 最后的零填充
    }

    // 为窗函数 h 创建一个零填充的向量 (在此实现中可能不是必需的，但保留与 MATLAB 行为一致)
    Eigen::VectorXd padded_h(hrow + 2 * Lh);
    for (long i = 0; i < Lh; ++i) {
        padded_h(i) = 0.0; // 最初的零填充
    }
    for (long i = 0; i < hrow; ++i) {
        padded_h(Lh + i) = h(i); // 原始窗函数 h
    }
    for (long i = 0; i < Lh; ++i) {
        padded_h(Lh + hrow + i) = 0.0; // 最后的零填充
    }

    // 计算 tfr 矩阵 (这里似乎是直接进行加窗和移位操作，而不是传统的卷积)
    double scale_factor = CFG.getDouble("detection_suppression.scale_factor", 32768.0);
    for (long i = 0; i < tcol; ++i) { // 遍历列（时间点）
        for (long j = 0; j < Lh; ++j) {
            tfr(N - Lh + j, i) = padded_x(i + j) * padded_h(Lh + j) / scale_factor;
        }
        for (long j = Lh; j < hrow; ++j) {
            tfr(j - Lh, i) = padded_x(i + j) * padded_h(Lh + j) / scale_factor;
        }
    }

    // 对每一列执行 FFT
    // 将 FFTW 资源设置为 thread_local 以避免在计划创建期间发生竞争条件
    thread_local fftw_complex* in = nullptr; // FFTW 输入缓冲区指针
    thread_local fftw_complex* out = nullptr; // FFTW 输出缓冲区指针
    thread_local fftw_plan p = nullptr;       // FFTW 计划
    thread_local int cached_N = 0;             // 用于存储创建计划时的 N 值

    // 创建 FFTW plan（如果尚未创建或 N 已更改）
    if (in == nullptr || cached_N != N) {
        if (p != nullptr) { // 如果 N 更改，销毁现有计划并释放内存
            fftw_destroy_plan(p);
            fftw_free(in);
            fftw_free(out);
        }
        in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
        out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
        p = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
        cached_N = N;
    }

    for (long col = 0; col < tcol; ++col) {
        // 准备输入数据：将 tfr 矩阵的当前列数据复制到 FFTW 输入缓冲区
        for (long row = 0; row < N; ++row) {
            in[row][0] = tfr(row, col).real();
            in[row][1] = tfr(row, col).imag();
        }

        // 执行计划：对当前列数据进行 FFT
        fftw_execute(p);

        // 获取结果：将 FFTW 输出缓冲区的结果复制回 tfr 矩阵的当前列
        for (long row = 0; row < N; ++row) {
            tfr(row, col) = std::complex<double>(out[row][0], out[row][1]);
        }
    }

    // 注意：这里没有销毁 FFTW 计划或释放内存，因为我们希望重用它们。
    // 它们将在线程退出时被释放。
    return tfr;
}

} // namespace TfrStft // TfrStft 命名空间结束

#endif // TFRSTFT_H // 头文件结束宏