#ifndef JAMMING_SIMULATOR_H // 防止头文件被重复包含
#define JAMMING_SIMULATOR_H // 定义宏，表示头文件已被包含

#include <Eigen/Dense>
#include "Config.h"
#define CFG Config::instance()
#include <random>
#include <complex>
#include <fftw3.h>
#include <cmath>

using namespace Eigen; // 使用 Eigen 命名空间，简化代码

/**
 * @brief 干扰信号模拟器类
 * 该类用于模拟不同类型的雷达干扰信号，包括间歇循环转发干扰 (ISCJ)、
 * 间歇直接转发干扰 (ISDJ)、间歇重复转发干扰 (ISRJ)、窄带瞄频干扰 (NBJ)
 * 和距离欺骗干扰 (RDJ)。
 */
class JammingSimulator {
public:
    /**
     * @brief 构造函数：初始化干扰模拟器的各项参数
     * @param fc 载波频率 (Hz)。
     * @param B 信号带宽 (Hz)。
     * @param fs 采样频率 (Hz)。
     * @param prf 脉冲重复频率 (Hz)。
     * @param Tp 脉冲宽度 (s)。
     * @param nan 噪声数量 (或 CPI 数量)。
     * @param R0 目标初始距离 (m)。
     * @param R1 干扰源距离 (m)。
     */
    inline JammingSimulator(double fc, double B, double fs, double prf, double Tp, int nan, double R0, double R1)
        : m_fc(fc), m_B(B), m_fs(fs), m_prf(prf), m_Tp(Tp), m_nan(nan), m_R0(R0), m_R1(R1) {
        // 使用随机设备初始化随机数生成器引擎的种子
        std::random_device rd;
        m_rng.seed(rd());
        // 计算调频斜率
        m_Kr = m_B / m_Tp;
        // 设置信号采样点数
        m_nrn = CFG.getInt("detection_suppression.nrn", 2048);
        // 调整时间轴向量的大小
        m_tnrn.resize(m_nrn);
        // 填充时间轴向量，考虑目标初始距离和采样频率
        for(int i = 0; i < m_nrn; ++i) {
            m_tnrn(i) = m_R0 / m_c + static_cast<double>(i) / m_fs;
        }
        // 初始化 FFTW 成员：分配输入/输出缓冲区并创建 FFT 计划
        m_in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * m_nrn);
        m_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * m_nrn);
        // 创建正向 FFT 计划 (从 m_in 到 m_out)
        m_p_fwd = fftw_plan_dft_1d(m_nrn, m_in, m_out, FFTW_FORWARD, FFTW_ESTIMATE);
        // 创建逆向 FFT 计划 (从 m_out 到 m_in)
        m_p_bwd = fftw_plan_dft_1d(m_nrn, m_out, m_in, FFTW_BACKWARD, FFTW_ESTIMATE);
    }

    /**
     * @brief 析构函数
     * 销毁 FFTW 计划并释放分配的内存。
     */
    inline ~JammingSimulator() {
        fftw_destroy_plan(m_p_fwd); // 销毁正向 FFT 计划
        fftw_destroy_plan(m_p_bwd); // 销毁逆向 FFT 计划
        fftw_free(m_in);            // 释放输入缓冲区内存
        fftw_free(m_out);           // 释放输出缓冲区内存
    }

    /**
     * @brief 生成间歇循环转发干扰 (ISCJ) 信号
     * @return 生成的 ISCJ 信号矩阵，每行代表一个 CPI 的信号。
     */
    inline MatrixXcd generateISCJ() {
        // 初始化一个零矩阵用于存储生成的 ISCJ 信号，大小为 m_nan (CPI 数量) x m_nrn (采样点数)
        MatrixXcd s_ISCJ = MatrixXcd::Zero(m_nan, m_nrn);
        // 定义 ISCJ 干扰的子脉冲宽度
        double T_ISCJ = CFG.getDouble("detection_suppression.iscj_sub_T", 1e-6);
        // 计算每个主脉冲内包含的子脉冲组数 N_ISCJ
        int N_ISCJ = floor((sqrt(9.0 + 8.0 * floor(m_Tp / T_ISCJ)) - 3.0) / 2.0);
        // 生成一个随机的频率偏移量
        double deltaf0 = unifrnd(-m_fs / 6.0, m_fs / 6.0);
        // 遍历每个 CPI (相干处理间隔)
        for (int m = 0; m < m_nan; ++m) {
            // 获取当前干扰源距离
            double R_ISCJ = m_R1;
            // 初始化一个零向量用于累加当前 CPI 的 ISCJ 信号
            RowVectorXcd s_ISCJ2 = RowVectorXcd::Zero(m_nrn);
            // 内部循环，生成不同组的子脉冲
            for (int Ni = 1; Ni <= N_ISCJ; ++Ni) {
                // 初始化一个零向量用于累加当前组的子脉冲信号
                RowVectorXcd s_ISCJ1 = RowVectorXcd::Zero(m_nrn);
                // 最内层循环，生成每个子脉冲
                for (int Nc = 1; Nc <= (N_ISCJ - Ni + 1); ++Nc) {
                    // 计算时间参数 b 和 a，用于确定子脉冲的起始时间
                    double b = (double)(Ni * (Ni + 1) + (Nc - 1) * (Nc + 2 * Ni + 2)) / 2.0;
                    double a = (double)((Nc - 1) * (Nc + 2 * Ni + 2)) / 2.0 + 1.0;
                    // 计算第一个时间项，用于确定子脉冲的有效窗口
                    ArrayXd term1 = m_tnrn.array() - 2 * R_ISCJ / m_c - b * T_ISCJ;
                    // 定义第一个窗口，判断信号是否在子脉冲宽度内
                    Array<bool, -1, 1> win1 = (term1 > 0) && (term1 < T_ISCJ);
                    // 计算第二个时间项，用于确定主脉冲的有效窗口
                    ArrayXd term2 = m_tnrn.array() - 2 * R_ISCJ / m_c - a * T_ISCJ;
                    // 定义第二个窗口，判断信号是否在主脉冲宽度内
                    Array<bool, -1, 1> winjj = (term2 > 0) && (term2 < m_Tp);
                    // 计算信号相位，包含线性调频、载波相位和频率偏移
                    ArrayXcd phase = m_j * M_PI * m_Kr * term2.pow(2) + m_j * 2.0 * M_PI * m_fc * term2 + m_j * 2.0 * deltaf0 * (m_tnrn.array() - 2 * R_ISCJ / m_c - T_ISCJ);
                    // 根据两个窗口的交集选择有效信号部分，并累加到 s_ISCJ1
                    s_ISCJ1 += (win1 && winjj).select(phase.exp(), 0.0).matrix();
                }
                // 累加当前组的子脉冲信号到 s_ISCJ2
                s_ISCJ2 += s_ISCJ1;
            }
            // 将当前 CPI 的 ISCJ 信号存储到最终的矩阵中
            s_ISCJ.row(m) = s_ISCJ2;
        }
        // 返回生成的 ISCJ 信号矩阵
        return s_ISCJ;
    }

    /**
     * @brief 生成间歇直接转发干扰 (ISDJ) 信号
     * @return 生成的 ISDJ 信号矩阵，每行代表一个 CPI 的信号。
     */
    inline MatrixXcd generateISDJ() {
        // 初始化一个零矩阵用于存储生成的 ISDJ 信号，大小为 m_nan (CPI 数量) x m_nrn (采样点数)
        MatrixXcd s_ISDJ = MatrixXcd::Zero(m_nan, m_nrn);
        // 定义 ISDJ 干扰的子脉冲宽度和重复周期
        double Ts_ISDJ = CFG.getDouble("detection_suppression.isdj_sub_Ts", 2e-6), T_ISDJ = 0.5 * Ts_ISDJ;
        // 计算每个主脉冲内包含的子脉冲数量 N_ISDJ
        int N_ISDJ = floor(m_Tp / Ts_ISDJ);
        // 生成一个随机的频率偏移量
        double deltaf0 = unifrnd(-m_fs / 6.0, m_fs / 6.0);
        // 遍历每个 CPI (相干处理间隔)
        for (int m = 0; m < m_nan; ++m) {
            // 获取当前干扰源距离
            double R_ISDJ = m_R1;
            // 初始化一个零向量用于累加当前 CPI 的 ISDJ 信号
            RowVectorXcd s_ISDJ1 = RowVectorXcd::Zero(m_nrn);
            // 循环生成每个子脉冲
            for (int Nc = 0; Nc < N_ISDJ; ++Nc) {
                // 计算第一个时间项，用于确定子脉冲的有效窗口
                ArrayXd term1 = m_tnrn.array() - 2 * R_ISDJ / m_c - (2.0 * Nc + 1.0) * T_ISDJ;
                // 定义第一个窗口，判断信号是否在子脉冲宽度内
                Array<bool, -1, 1> win1 = (term1 > 0) && (term1 < T_ISDJ);
                // 计算第二个时间项，用于确定主脉冲的有效窗口
                ArrayXd term2 = m_tnrn.array() - 2 * R_ISDJ / m_c - T_ISDJ;
                // 定义第二个窗口，判断信号是否在主脉冲宽度内
                Array<bool, -1, 1> winjj = (term2 > 0) && (term2 < m_Tp);
                // 计算信号相位，包含线性调频、载波相位和频率偏移
                ArrayXcd phase = m_j * M_PI * m_Kr * term2.pow(2) + m_j * 2.0 * M_PI * m_fc * term2 + m_j * 2.0 * deltaf0 * term2;
                // 根据两个窗口的交集选择有效信号部分，并累加到 s_ISDJ1
                s_ISDJ1 += (win1 && winjj).select(phase.exp(), 0.0).matrix();
            }
            // 将当前 CPI 的 ISDJ 信号存储到最终的矩阵中
            s_ISDJ.row(m) = s_ISDJ1;
        }
        // 返回生成的 ISDJ 信号矩阵
        return s_ISDJ;
    }

    /**
     * @brief 生成间歇重复转发干扰 (ISRJ) 信号
     * @return 生成的 ISRJ 信号矩阵，每行代表一个 CPI 的信号。
     */
    inline MatrixXcd generateISRJ() {
        // 初始化一个零矩阵用于存储生成的 ISRJ 信号，大小为 m_nan (CPI 数量) x m_nrn (采样点数)
        MatrixXcd s_ISRJ = MatrixXcd::Zero(m_nan, m_nrn);
        // 定义 ISRJ 干扰的子脉冲宽度和重复周期
        double Ts_ISRJ = CFG.getDouble("detection_suppression.isrj_sub_Ts", 4e-6), T_ISRJ = 0.25 * Ts_ISRJ;
        // 计算每个主脉冲内包含的子脉冲数量 N_ISRJ
        int N_ISRJ = floor(m_Tp / Ts_ISRJ);
        // 计算每个重复周期内包含的子脉冲数量 Num_C_I
        int Num_C_I = floor(Ts_ISRJ / T_ISRJ) - 1;
        // 生成一个随机的频率偏移量
        double deltaf0 = unifrnd(-m_fs / 6.0, m_fs / 6.0);
        // 遍历每个 CPI (相干处理间隔)
        for (int m = 0; m < m_nan; ++m) {
            // 获取当前干扰源距离
            double R_ISRJ = m_R1;
            // 初始化一个零向量用于累加当前 CPI 的 ISRJ 信号
            RowVectorXcd s_ISRJ2 = RowVectorXcd::Zero(m_nrn);
            // 内部循环，生成不同组的子脉冲
            for (int Ni = 1; Ni <= Num_C_I; ++Ni) {
                // 初始化一个零向量用于累加当前组的子脉冲信号
                RowVectorXcd s_ISRJ1 = RowVectorXcd::Zero(m_nrn);
                // 最内层循环，生成每个子脉冲           
                for (int Nc = 0; Nc < N_ISRJ; ++Nc) {
                    // 计算第一个时间项，用于确定子脉冲的有效窗口
                    ArrayXd term1 = m_tnrn.array() - 2 * R_ISRJ / m_c - Nc * Ts_ISRJ - Ni * T_ISRJ;
                    // 定义第一个窗口，判断信号是否在子脉冲宽度内
                    Array<bool, -1, 1> win1 = (term1 > 0) && (term1 < T_ISRJ);
                    // 计算第二个时间项，用于确定主脉冲的有效窗口
                    ArrayXd term2 = m_tnrn.array() - 2 * R_ISRJ / m_c - Ni * T_ISRJ;
                    // 定义第二个窗口，判断信号是否在主脉冲宽度内
                    Array<bool, -1, 1> winjj = (term2 > 0) && (term2 < m_Tp);
                    // 计算信号相位，包含线性调频、载波相位和频率偏移
                    ArrayXcd phase = m_j * M_PI * m_Kr * term2.pow(2) + m_j * 2.0 * M_PI * m_fc * term2 + m_j * 2.0 * deltaf0 * (m_tnrn.array() - 2 * R_ISRJ / m_c - T_ISRJ);
                    // 根据两个窗口的交集选择有效信号部分，并累加到 s_ISRJ1
                    s_ISRJ1 += (win1 && winjj).select(phase.exp(), 0.0).matrix();
                }
                // 累加当前组的子脉冲信号到 s_ISRJ2
                s_ISRJ2 += s_ISRJ1;
            }
            // 将当前 CPI 的 ISRJ 信号存储到最终的矩阵中
            s_ISRJ.row(m) = s_ISRJ2;
        }
        // 返回生成的 ISRJ 信号矩阵
        return s_ISRJ;
    }

    /**
     * @brief 生成窄带瞄频干扰 (NBJ) 信号
     * @param j_Br 干扰带宽。
     * @return 生成的 NBJ 信号矩阵，每行代表一个 CPI 的信号。
     */
    inline MatrixXcd generateNBJ() {
        MatrixXcd j_matrix = MatrixXcd::Zero(m_nan, m_nrn);
        double nbj_noise_std = CFG.getDouble("detection_suppression.nbj_noise_std", 5.0);
        int nbj_filter_order = CFG.getInt("detection_suppression.nbj_filter_order", 8);
        std::normal_distribution<double> norm_dist(0.0, sqrt(nbj_noise_std));
        for (int m = 0; m < m_nan; ++m) {
            for (int k = 0; k < m_nrn; ++k) { m_in[k][0] = norm_dist(m_rng); m_in[k][1] = norm_dist(m_rng); }
            fftw_execute(m_p_fwd);
            double j_Br = unifrnd(m_B/16.0, m_B/4.0);
            for (int k = 0; k < m_nrn; ++k) {
                double freq_k = (k < m_nrn/2) ? (double)k*m_fs/m_nrn : (double)(k-m_nrn)*m_fs/m_nrn;
                double H = 1.0 / sqrt(1.0 + pow(freq_k / (j_Br/2.0), 2.0 * nbj_filter_order));
                m_out[k][0] *= H; m_out[k][1] *= H;
            }
            fftw_execute(m_p_bwd);
            RowVectorXcd lvbo_z(m_nrn);
            for (int k=0; k<m_nrn; ++k) lvbo_z(k) = {m_in[k][0]/m_nrn, m_in[k][1]/m_nrn};
            j_matrix.row(m) = lvbo_z / lvbo_z.cwiseAbs().maxCoeff();
        }
        double deltaf0 = CFG.getDouble("detection_suppression.nbj_center_freq", 15e6);
        using ArrayXcdRow = Eigen::Array<std::complex<double>, 1, Eigen::Dynamic>;
        ArrayXcdRow modulation = (m_j * 2.0 * M_PI * deltaf0 * ((m_tnrn.transpose()).array() - 2*m_R0/m_c)).exp();
        return j_matrix.array().rowwise() * modulation.array();
    }

    /**
     * @brief 生成距离欺骗干扰 (RDJ) 信号
     * @return 生成的 RDJ 信号矩阵，每行代表一个 CPI 的信号。
     */
    inline MatrixXcd generateRDJ() {
        // 初始化一个零矩阵用于存储生成的 RDJ 信号，大小为 m_nan (CPI 数量) x m_nrn (采样点数)
        MatrixXcd jammingsignal = MatrixXcd::Zero(m_nan, m_nrn);
        // 生成一个随机的频率偏移量
        double deltaf0 = unifrnd(-m_fs/4.0, m_fs/4.0);
        // 遍历每个 CPI (相干处理间隔)
        for (int m = 0; m < m_nan; ++m) {
            // 获取当前干扰源距离
            double R_t = m_R1;
            // 计算时间项，用于确定信号的相位和有效窗口
            ArrayXd term = m_tnrn.array() - 2*R_t/m_c;
            // 定义信号的有效窗口，通常在脉冲宽度的一部分内
            Array<bool, -1, 1> win = (term > m_Tp/4.0) && (term < m_Tp*3.0/4.0);
            // 计算信号相位，包含线性调频、载波相位和频率偏移
            ArrayXcd phase = m_j*M_PI*m_Kr*term.pow(2) + m_j*2.0*M_PI*m_fc*term + m_j*2.0*deltaf0*term;
            // 根据有效窗口选择信号部分，并存储到最终的矩阵中
            jammingsignal.row(m) = win.select(phase.exp(), 0.0).matrix();
        }
        // 返回生成的 RDJ 信号矩阵
        return jammingsignal;
    }

private:
    // 成员变量：存储干扰模拟器所需的各种参数
    double m_fc, m_B, m_fs, m_prf, m_Tp; // 载波频率、信号带宽、采样频率、脉冲重复频率、脉冲宽度
    int m_nan;                           // 噪声数量 (或 CPI 数量)
    double m_R0, m_R1;                   // 目标初始距离、干扰源距离
    double m_Kr;                         // 调频斜率
    int m_nrn;                           // 信号采样点数
    VectorXd m_tnrn;                     // 时间轴向量
    const double m_c = 3e8;              // 光速
    const std::complex<double> m_j{0.0, 1.0}; // 虚数单位
    std::mt19937 m_rng;                  // 随机数生成器引擎

    fftw_complex *m_in; // FFTW 输入缓冲区指针
    fftw_complex *m_out; // FFTW 输出缓冲区指针
    fftw_plan m_p_fwd;   // 正向 FFTW 计划
    fftw_plan m_p_bwd;   // 逆向 FFTW 计划

    /**
     * @brief 辅助函数：生成指定范围内的均匀分布随机数
     * @param min 最小值。
     * @param max 最大值。
     * @return 在 [min, max] 范围内的一个均匀分布随机数。
     */
    inline double unifrnd(double min, double max) {
        return std::uniform_real_distribution<double>(min, max)(m_rng);
    }
};

#endif // JAMMING_SIMULATOR_H // 头文件结束宏