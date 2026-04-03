// ============================================================================
//  JammingSimulator.h — 干扰信号模拟器 (从03模块移植,适配04模块参数体系)
//  Jamming Signal Simulator — ported from Module03, adapted for Module04
// ============================================================================
//
//  本文件以 03_jamming_detection_suppression/include/JammingSimulator.h 为蓝本,
//  原样保留全部算法逻辑,仅做以下适配:
//    1. 移除对 Config.h 的运行时依赖,改为构造函数传入全部参数
//    2. 子脉冲参数(iscj_sub_T等)改为构造函数可选参数,提供与03模块相同的默认值
//    3. 输出格式不变: MatrixXcd (nan × nrn), 每行一个CPI
//
//  算法原理(与03模块完全一致):
//    ISDJ — 间歇直接转发: 均匀采样原始脉冲,直接转发
//    ISRJ — 间歇重复转发: 均匀采样后多拍延迟转发
//    ISCJ — 间歇循环转发: 非均匀采样+递增拍数转发
//    NBJ  — 窄带瞄频噪声: 白噪声经低通滤波后载波调制
//    RDJ  — 距离欺骗: 截取脉冲片段转发,附加频偏
//
//  依赖: Eigen/Dense, fftw3.h, <random>, <complex>, <cmath>
//
// ============================================================================

#ifndef MODULE4_JAMMING_SIMULATOR_H
#define MODULE4_JAMMING_SIMULATOR_H

#include <Eigen/Dense>
#include <random>
#include <complex>
#include <cmath>
#include <fftw3.h>

using namespace Eigen;

/**
 * @brief 干扰信号模拟器类 (04模块版本,无Config依赖)
 *
 * 所有信号参数通过构造函数传入,算法逻辑与03模块JamTarDivi完全一致.
 * 输出: MatrixXcd (cpiNum × nrn), 每行一个CPI的干扰信号.
 */
class JammingSimulator4 {
public:
    /**
     * @brief 构造函数: 初始化干扰模拟器
     * @param fc       载波频率 (Hz)
     * @param B        信号带宽 (Hz)
     * @param fs       采样频率 (Hz)
     * @param prf      脉冲重复频率 (Hz), 当前未直接使用但保留接口
     * @param Tp       脉冲宽度 (s)
     * @param cpiNum   CPI数量(即需要生成干扰的脉冲数)
     * @param R0       目标初始距离 (m), 用于时间轴计算
     * @param R1       干扰源距离 (m), 用于干扰时延
     * @param nrn_val  距离向采样点数
     * @param iscj_sub_T    ISCJ子脉冲宽度 (s), 默认1e-6
     * @param isdj_sub_Ts   ISDJ子脉冲周期 (s), 默认2e-6
     * @param isrj_sub_Ts   ISRJ子脉冲周期 (s), 默认4e-6
     * @param nbj_noise_std NBJ噪声标准差, 默认5.0
     * @param nbj_filter_order NBJ滤波器阶数, 默认8
     * @param nbj_center_freq   NBJ中心频偏 (Hz), 默认15e6
     */
    inline JammingSimulator4(double fc, double B, double fs, double prf, double Tp,
                              int cpiNum, double R0, double R1, int nrn_val,
                              double iscj_sub_T = 1e-6,
                              double isdj_sub_Ts = 2e-6,
                              double isrj_sub_Ts = 4e-6,
                              double nbj_noise_std = 5.0,
                              int nbj_filter_order = 8,
                              double nbj_center_freq = 15e6)
        : m_fc(fc), m_B(B), m_fs(fs), m_prf(prf), m_Tp(Tp),
          m_nan(cpiNum), m_R0(R0), m_R1(R1), m_nrn(nrn_val),
          m_iscj_sub_T(iscj_sub_T), m_isdj_sub_Ts(isdj_sub_Ts), m_isrj_sub_Ts(isrj_sub_Ts),
          m_nbj_noise_std(nbj_noise_std), m_nbj_filter_order(nbj_filter_order),
          m_nbj_center_freq(nbj_center_freq)
    {
        std::random_device rd;
        m_rng.seed(rd());
        m_Kr = m_B / m_Tp;

        // 构造时间轴(适配04模块): tnrn(i) = Tstart + i/fs
        // 04模块采样窗口以 2*R0/c 为中心: Tstart = 2*R0/c - nrn/(2*fs)
        m_tnrn.resize(m_nrn);
        double Tstart = 2.0 * m_R0 / m_c - m_nrn / 2.0 / m_fs;
        for (int i = 0; i < m_nrn; ++i) {
            m_tnrn(i) = Tstart + static_cast<double>(i) / m_fs;
        }

        // FFTW 初始化
        m_in  = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * m_nrn);
        m_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * m_nrn);
        m_p_fwd = fftw_plan_dft_1d(m_nrn, m_in, m_out, FFTW_FORWARD,  FFTW_ESTIMATE);
        m_p_bwd = fftw_plan_dft_1d(m_nrn, m_out, m_in, FFTW_BACKWARD, FFTW_ESTIMATE);
    }

    inline ~JammingSimulator4() {
        fftw_destroy_plan(m_p_fwd);
        fftw_destroy_plan(m_p_bwd);
        fftw_free(m_in);
        fftw_free(m_out);
    }

    // ── 间歇循环转发干扰 (ISCJ) ──
    // 算法与03模块完全一致: 递增拍数的非均匀采样
    inline MatrixXcd generateISCJ() {
        MatrixXcd s_ISCJ = MatrixXcd::Zero(m_nan, m_nrn);
        double T_ISCJ = m_iscj_sub_T;
        int N_ISCJ = static_cast<int>(floor((sqrt(9.0 + 8.0 * floor(m_Tp / T_ISCJ)) - 3.0) / 2.0));
        double deltaf0 = unifrnd(-m_fs / 6.0, m_fs / 6.0);

        for (int m = 0; m < m_nan; ++m) {
            double R_ISCJ = m_R1;
            RowVectorXcd s_ISCJ2 = RowVectorXcd::Zero(m_nrn);

            for (int Ni = 1; Ni <= N_ISCJ; ++Ni) {
                RowVectorXcd s_ISCJ1 = RowVectorXcd::Zero(m_nrn);
                for (int Nc = 1; Nc <= (N_ISCJ - Ni + 1); ++Nc) {
                    double b = static_cast<double>(Ni * (Ni + 1) + (Nc - 1) * (Nc + 2 * Ni + 2)) / 2.0;
                    double a = static_cast<double>((Nc - 1) * (Nc + 2 * Ni + 2)) / 2.0 + 1.0;
                    ArrayXd term1 = m_tnrn.array() - 2 * R_ISCJ / m_c - b * T_ISCJ;
                    Array<bool, -1, 1> win1 = (term1 > 0) && (term1 < T_ISCJ);
                    ArrayXd term2 = m_tnrn.array() - 2 * R_ISCJ / m_c - a * T_ISCJ;
                    Array<bool, -1, 1> winjj = (term2 > 0) && (term2 < m_Tp);
                    ArrayXcd phase = m_j * M_PI * m_Kr * term2.pow(2)
                                   + m_j * 2.0 * M_PI * m_fc * term2
                                   + m_j * 2.0 * deltaf0 * (m_tnrn.array() - 2 * R_ISCJ / m_c - T_ISCJ);
                    s_ISCJ1 += (win1 && winjj).select(phase.exp(), 0.0).matrix();
                }
                s_ISCJ2 += s_ISCJ1;
            }
            s_ISCJ.row(m) = s_ISCJ2;
        }
        return s_ISCJ;
    }

    // ── 间歇直接转发干扰 (ISDJ) ──
    // 算法与03模块完全一致: 均匀等间隔采样转发
    inline MatrixXcd generateISDJ() {
        MatrixXcd s_ISDJ = MatrixXcd::Zero(m_nan, m_nrn);
        double Ts_ISDJ = m_isdj_sub_Ts;
        double T_ISDJ  = 0.5 * Ts_ISDJ;
        int N_ISDJ = static_cast<int>(floor(m_Tp / Ts_ISDJ));
        double deltaf0 = unifrnd(-m_fs / 6.0, m_fs / 6.0);

        for (int m = 0; m < m_nan; ++m) {
            double R_ISDJ = m_R1;
            RowVectorXcd s_ISDJ1 = RowVectorXcd::Zero(m_nrn);
            for (int Nc = 0; Nc < N_ISDJ; ++Nc) {
                ArrayXd term1 = m_tnrn.array() - 2 * R_ISDJ / m_c - (2.0 * Nc + 1.0) * T_ISDJ;
                Array<bool, -1, 1> win1 = (term1 > 0) && (term1 < T_ISDJ);
                ArrayXd term2 = m_tnrn.array() - 2 * R_ISDJ / m_c - T_ISDJ;
                Array<bool, -1, 1> winjj = (term2 > 0) && (term2 < m_Tp);
                ArrayXcd phase = m_j * M_PI * m_Kr * term2.pow(2)
                               + m_j * 2.0 * M_PI * m_fc * term2
                               + m_j * 2.0 * deltaf0 * term2;
                s_ISDJ1 += (win1 && winjj).select(phase.exp(), 0.0).matrix();
            }
            s_ISDJ.row(m) = s_ISDJ1;
        }
        return s_ISDJ;
    }

    // ── 间歇重复转发干扰 (ISRJ) ──
    // 算法与03模块完全一致: 多拍延迟重复转发
    inline MatrixXcd generateISRJ() {
        MatrixXcd s_ISRJ = MatrixXcd::Zero(m_nan, m_nrn);
        double Ts_ISRJ = m_isrj_sub_Ts;
        double T_ISRJ  = 0.25 * Ts_ISRJ;
        int N_ISRJ = static_cast<int>(floor(m_Tp / Ts_ISRJ));
        int Num_C_I = static_cast<int>(floor(Ts_ISRJ / T_ISRJ)) - 1;
        double deltaf0 = unifrnd(-m_fs / 6.0, m_fs / 6.0);

        for (int m = 0; m < m_nan; ++m) {
            double R_ISRJ = m_R1;
            RowVectorXcd s_ISRJ2 = RowVectorXcd::Zero(m_nrn);
            for (int Ni = 1; Ni <= Num_C_I; ++Ni) {
                RowVectorXcd s_ISRJ1 = RowVectorXcd::Zero(m_nrn);
                for (int Nc = 0; Nc < N_ISRJ; ++Nc) {
                    ArrayXd term1 = m_tnrn.array() - 2 * R_ISRJ / m_c - Nc * Ts_ISRJ - Ni * T_ISRJ;
                    Array<bool, -1, 1> win1 = (term1 > 0) && (term1 < T_ISRJ);
                    ArrayXd term2 = m_tnrn.array() - 2 * R_ISRJ / m_c - Ni * T_ISRJ;
                    Array<bool, -1, 1> winjj = (term2 > 0) && (term2 < m_Tp);
                    ArrayXcd phase = m_j * M_PI * m_Kr * term2.pow(2)
                                   + m_j * 2.0 * M_PI * m_fc * term2
                                   + m_j * 2.0 * deltaf0 * (m_tnrn.array() - 2 * R_ISRJ / m_c - T_ISRJ);
                    s_ISRJ1 += (win1 && winjj).select(phase.exp(), 0.0).matrix();
                }
                s_ISRJ2 += s_ISRJ1;
            }
            s_ISRJ.row(m) = s_ISRJ2;
        }
        return s_ISRJ;
    }

    // ── 窄带瞄频噪声干扰 (NBJ) ──
    // 算法与03模块完全一致: 白噪声→FFT→低通滤波→IFFT→归一化→载波调制
    inline MatrixXcd generateNBJ() {
        MatrixXcd j_matrix = MatrixXcd::Zero(m_nan, m_nrn);
        std::normal_distribution<double> norm_dist(0.0, sqrt(m_nbj_noise_std));

        for (int m = 0; m < m_nan; ++m) {
            for (int k = 0; k < m_nrn; ++k) {
                m_in[k][0] = norm_dist(m_rng);
                m_in[k][1] = norm_dist(m_rng);
            }
            fftw_execute(m_p_fwd);
            double j_Br = unifrnd(m_B / 16.0, m_B / 4.0);
            for (int k = 0; k < m_nrn; ++k) {
                double freq_k = (k < m_nrn / 2)
                    ? static_cast<double>(k) * m_fs / m_nrn
                    : static_cast<double>(k - m_nrn) * m_fs / m_nrn;
                double H = 1.0 / sqrt(1.0 + pow(freq_k / (j_Br / 2.0), 2.0 * m_nbj_filter_order));
                m_out[k][0] *= H;
                m_out[k][1] *= H;
            }
            fftw_execute(m_p_bwd);
            RowVectorXcd lvbo_z(m_nrn);
            for (int k = 0; k < m_nrn; ++k) {
                lvbo_z(k) = std::complex<double>(m_in[k][0] / m_nrn, m_in[k][1] / m_nrn);
            }
            j_matrix.row(m) = lvbo_z / lvbo_z.cwiseAbs().maxCoeff();
        }

        double deltaf0 = m_nbj_center_freq;
        using ArrayXcdRow = Eigen::Array<std::complex<double>, 1, Eigen::Dynamic>;
        ArrayXcdRow modulation = (m_j * 2.0 * M_PI * deltaf0 * (m_tnrn.transpose().array() - 2 * m_R0 / m_c)).exp();
        return j_matrix.array().rowwise() * modulation.array();
    }

    // ── 距离欺骗干扰 (RDJ) ──
    // 算法与03模块完全一致: 截取脉冲中段,附加随机频偏
    inline MatrixXcd generateRDJ() {
        MatrixXcd jammingsignal = MatrixXcd::Zero(m_nan, m_nrn);
        double deltaf0 = unifrnd(-m_fs / 4.0, m_fs / 4.0);

        for (int m = 0; m < m_nan; ++m) {
            double R_t = m_R1;
            ArrayXd term = m_tnrn.array() - 2 * R_t / m_c;
            Array<bool, -1, 1> win = (term > m_Tp / 4.0) && (term < m_Tp * 3.0 / 4.0);
            ArrayXcd phase = m_j * M_PI * m_Kr * term.pow(2)
                           + m_j * 2.0 * M_PI * m_fc * term
                           + m_j * 2.0 * deltaf0 * term;
            jammingsignal.row(m) = win.select(phase.exp(), 0.0).matrix();
        }
        return jammingsignal;
    }

private:
    double m_fc, m_B, m_fs, m_prf, m_Tp;
    int    m_nan;
    double m_R0, m_R1;
    double m_Kr;
    int    m_nrn;
    VectorXd m_tnrn;
    const double m_c = 3e8;
    const std::complex<double> m_j{0.0, 1.0};
    std::mt19937 m_rng;

    // 子脉冲参数
    double m_iscj_sub_T;
    double m_isdj_sub_Ts;
    double m_isrj_sub_Ts;
    double m_nbj_noise_std;
    int    m_nbj_filter_order;
    double m_nbj_center_freq;

    // FFTW 资源
    fftw_complex *m_in, *m_out;
    fftw_plan m_p_fwd, m_p_bwd;

    inline double unifrnd(double min_val, double max_val) {
        return std::uniform_real_distribution<double>(min_val, max_val)(m_rng);
    }
};

#endif // MODULE4_JAMMING_SIMULATOR_H
