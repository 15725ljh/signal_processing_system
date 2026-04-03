// ============================================================================
//  TfrStft.h — 短时傅里叶变换 (STFT) 统一实现
//  Time-Frequency Representation via Short-Time Fourier Transform
// ============================================================================
//
//  本文件为全系统统一的 STFT 实现,以 03_jamming_detection_suppression 模块的
//  TfrStft.h 为蓝本提取至 common/ 目录,供所有模块共享使用.
//
//  算法原理:
//    STFT 将一维时域信号分解为二维时频平面,通过对信号加窗后逐段执行 FFT 实现.
//    窗函数在时间轴上滑动,每一段产生频域的一列,所有列拼接即为时频表示矩阵.
//
//  与各模块旧版 tfrstft() 的关键差异:
//    1. 返回值: 直接返回 MatrixXcd,不再通过引用参数输出(更安全的接口设计)
//    2. 零填充策略: 对输入信号和窗函数两端进行 Lh 点零填充,确保边界处加窗完整
//    3. 缩放因子: 除以 scale_factor(默认 32768=2^15)防止 FFT 后数值溢出
//    4. FFTW plan 缓存: 使用 thread_local 缓存 FFTW plan,同一 N 值仅创建一次,
//       多次调用时避免重复创建/销毁 plan 的开销
//    5. 不生成频率向量 f: 调用方如需频率轴可自行构造
//
//  依赖:
//    - Eigen/Dense       矩阵运算
//    - Config.h          配置管理器(读取 scale_factor)
//    - fftw3.h           FFTW3 快速傅里叶变换库
//
//  配置项:
//    detection_suppression.scale_factor  STFT 内部缩放因子,默认 32768.0
//
//  使用示例:
//    Eigen::VectorXcd signal = ...;                            // 输入信号(nrn×1)
//    Eigen::VectorXd t = Eigen::VectorXd::LinSpaced(nrn, 1, nrn); // 时间轴
//    Eigen::VectorXd h = hamming_window(31);                   // 窗函数
//    Eigen::MatrixXcd tfr = TfrStft::tfrstft(signal, t, 256, h); // STFT结果(256×nrn)
//
// ============================================================================

#ifndef COMMON_TFRSTFT_H
#define COMMON_TFRSTFT_H

#include <Eigen/Dense>        // Eigen线性代数库(MatrixXcd, VectorXcd, VectorXd等)
#include "Config.h"           // 配置管理器(读取detection_suppression.scale_factor)
#include <vector>             // STL动态数组
#include <complex>            // 复数类型(std::complex<double>)
#include <cmath>              // 数学函数
#include <fftw3.h>            // FFTW3快速傅里叶变换库

namespace TfrStft {

/**
 * @brief 执行短时傅里叶变换 (STFT)
 *
 * 将一维复数信号分解为二维时频表示矩阵.对信号进行零填充后,在每个时间点上
 * 对信号段加窗,然后执行 FFT 得到该时刻的频谱.所有时刻的频谱按列排列,形成
 * 时频图 tfr,其中行对应频率维度,列对应时间维度.
 *
 * 算法步骤:
 *   1. 计算窗函数半长 Lh = (窗长-1)/2
 *   2. 对输入信号 x 和窗函数 h 进行两端 Lh 点零填充
 *   3. 在每个时间点 i,将零填充信号段 x[i..i+2*Lh] 与窗函数逐元素相乘,
 *      结果按循环移位写入 tfr 矩阵的第 i 列(加窗短时信号)
 *   4. 对 tfr 的每一列执行 FFT(使用 FFTW3),得到最终的时频表示
 *
 * @param x  输入的复数信号向量,维度为 xrow×1 (例如 nrn×1,单个脉冲的距离向采样)
 * @param t  时间轴向量,用于确定 STFT 的时间点数量,维度为 tcol×1 (通常等于 xrow)
 * @param N  FFT 点数,决定频率分辨率 (例如 256).N 越大频率分辨率越高,但计算量增加
 * @param h  窗函数向量(例如汉明窗),用于对信号进行局部化加权,减少频谱泄漏
 * @return   时频表示矩阵,维度为 N×tcol (行=频率,列=时间),复数类型
 *
 * @note  FFTW plan 使用 thread_local 缓存,同一 N 值下多次调用仅首次创建 plan,
 *        后续调用复用已有 plan,显著减少重复调用的开销.
 * @note  内部使用 scale_factor(默认 32768=2^15)对加窗后的信号进行缩放,
 *        防止 FFT 后数值过大导致精度损失.
 */
inline Eigen::MatrixXcd tfrstft(const Eigen::VectorXcd& x, const Eigen::VectorXd& t, int N, const Eigen::VectorXd& h) {

    long xrow = x.size();       // 输入信号长度(距离向采样点数,如 nrn=2048)
    long tcol = t.size();       // 时间轴长度(STFT 的时间点数,通常等于 xrow)
    long hrow = h.size();       // 窗函数长度(如 31)
    long Lh = (hrow - 1) / 2;  // 窗函数半长(如 (31-1)/2=15),用于零填充和对齐

    // 初始化时频矩阵,维度 N(频率) × tcol(时间),初始值全零
    Eigen::MatrixXcd tfr = Eigen::MatrixXcd::Zero(N, tcol);

    // ---- 第一步: 对输入信号 x 进行零填充 ----
    // 在信号两端各填充 Lh 个零,确保窗函数滑动到边界时仍能完整覆盖信号段
    // 填充后长度: xrow + 2*Lh
    Eigen::VectorXcd padded_x(xrow + 2 * Lh);
    for (long i = 0; i < Lh; ++i) {
        padded_x(i) = 0.0;                    // 前端 Lh 个零
    }
    for (long i = 0; i < xrow; ++i) {
        padded_x(Lh + i) = x(i);              // 原始信号
    }
    for (long i = 0; i < Lh; ++i) {
        padded_x(Lh + xrow + i) = 0.0;        // 后端 Lh 个零
    }

    // ---- 第二步: 对窗函数 h 进行零填充 ----
    // 窗函数同样进行两端 Lh 点零填充,使其长度与 padded_x 对齐
    // 填充后长度: hrow + 2*Lh
    Eigen::VectorXd padded_h(hrow + 2 * Lh);
    for (long i = 0; i < Lh; ++i) {
        padded_h(i) = 0.0;                    // 前端 Lh 个零
    }
    for (long i = 0; i < hrow; ++i) {
        padded_h(Lh + i) = h(i);              // 原始窗函数
    }
    for (long i = 0; i < Lh; ++i) {
        padded_h(Lh + hrow + i) = 0.0;        // 后端 Lh 个零
    }

    // ---- 第三步: 加窗处理(短时截取) ----
    // 对每个时间点 i,将零填充信号段 padded_x[i..i+hrow+Lh-1] 与窗函数 padded_h[Lh..Lh+hrow-1]
    // 逐元素相乘,结果通过循环索引写入 tfr 矩阵的第 i 列
    // 循环索引 (N + tau) % N 将加窗信号映射到 N 点频率区间
    // 除以 scale_factor 防止 FFT 后数值过大
    double scale_factor = Config::instance().getDouble("detection_suppression.scale_factor", 32768.0);

    for (long i = 0; i < tcol; ++i) {         // 遍历每个时间点(列)
        // 窗函数前半段(j < Lh): 对应负频率偏移,写入 tfr 的高频区(N-Lh+j)
        for (long j = 0; j < Lh; ++j) {
            tfr(N - Lh + j, i) = padded_x(i + j) * padded_h(Lh + j) / scale_factor;
        }
        // 窗函数后半段(j >= Lh): 对应正频率偏移,写入 tfr 的低频区(j-Lh)
        for (long j = Lh; j < hrow; ++j) {
            tfr(j - Lh, i) = padded_x(i + j) * padded_h(Lh + j) / scale_factor;
        }
    }

    // ---- 第四步: 对每一列执行 FFT ----
    // 使用 FFTW3 库对 tfr 的每一列执行一维 FFT,将加窗时域信号转换到频域
    // FFTW plan 使用 thread_local 缓存,同一 N 值下仅首次创建,后续复用
    thread_local fftw_complex* in = nullptr;   // FFTW 输入缓冲区(复数数组)
    thread_local fftw_complex* out = nullptr;  // FFTW 输出缓冲区(复数数组)
    thread_local fftw_plan p = nullptr;        // FFTW 计算计划(包含优化信息)
    thread_local int cached_N = 0;             // 上次创建 plan 时的 N 值,用于判断是否需要重建

    // 如果 plan 尚未创建,或 N 值发生变化,则需要创建新的 plan
    if (in == nullptr || cached_N != N) {
        if (p != nullptr) {                    // 销毁旧的 plan 和缓冲区(防止内存泄漏)
            fftw_destroy_plan(p);
            fftw_free(in);
            fftw_free(out);
        }
        in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);   // 分配输入缓冲区
        out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);  // 分配输出缓冲区
        p = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE); // 创建 FFT plan
        cached_N = N;                         // 记录当前 N 值
    }

    // 对 tfr 的每一列执行 FFT: tfr的列 → in → FFTW执行 → out → 写回tfr的列
    for (long col = 0; col < tcol; ++col) {    // 遍历每个时间点(列)
        // 将 Eigen 矩阵的当前列数据复制到 FFTW 输入缓冲区
        for (long row = 0; row < N; ++row) {
            in[row][0] = tfr(row, col).real();  // 实部
            in[row][1] = tfr(row, col).imag();  // 虚部
        }
        // 执行 FFT(FFTW_FORWARD = 正变换,时域→频域)
        fftw_execute(p);
        // 将 FFTW 输出结果复制回 Eigen 矩阵
        for (long row = 0; row < N; ++row) {
            tfr(row, col) = std::complex<double>(out[row][0], out[row][1]);
        }
    }

    // 注意: FFTW plan 和缓冲区不在此处销毁,由 thread_local 机制在线程退出时自动释放
    // 这样在多次调用 tfrstft() 时可以复用 plan,避免重复创建的开销

    return tfr;  // 返回时频表示矩阵,维度 N×tcol
}

} // namespace TfrStft

#endif // COMMON_TFRSTFT_H
