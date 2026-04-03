/*
 * 模块04 - 信号处理实现 (chuli)
 *
 * 功能: 自动遍历6种信号处理模式(Case1~6),每个Case独立加载匹配的输入数据
 *       Case1~5从模块01加载对应波形, Case6从模块02加载含干扰回波
 *
 * 数据流:
 *   Case1 ← 01_waveform_Case1_信号矩阵 + 01_waveform_Case1_跳频序列
 *   Case2 ← 01_waveform_Case2_信号矩阵 + 01_waveform_Case2_随机相位序列
 *   Case3 ← 01_waveform_Case3_信号矩阵
 *   Case4 ← 01_waveform_Case4_信号矩阵
 *   Case5 ← 01_waveform_Case5_信号矩阵 + 跳频序列 + 随机相位序列
 *   Case6 ← 02_jamming_Case*_含干扰回波 (或EchoGenerator4兜底)
 *
 * 依赖: Module0.h(工具函数+全局参数), Module3.h(处理函数), EchoGenerator4.h(兜底), Config.h
 */
#include "Module0.h"          // 工具函数(loadMatrix/loadVector等) + 全局参数
#include "Module3.h"          // 信号处理函数声明(chuli + Case1~6)
#include "EchoGenerator4.h"   // Case6兜底信号生成
#include "Config.h"           // 配置管理器

// ── 为指定Case加载匹配数据 ──
// Case1~5: 从模块01加载对应波形 + 辅助序列
// Case6:   从模块02加载含干扰回波,无数据则用EchoGenerator4兜底
// 返回: true=加载成功, false=无匹配数据(调用方应跳过)
static bool load_case_data(int mode) {
    const int _nan1 = nan1();
    const int _nrn = nrn();

    // 重置全局变量
    Radar_Sig.setZero();
    f = VectorXd::Zero(_nan1);
    phi1 = VectorXcd::Ones(_nan1);

    if (mode >= 1 && mode <= 5) {
        // ── Case1~5: 从模块01加载匹配波形 ──
        ostringstream oss;
        oss << "01_waveform_Case" << mode << "_信号矩阵_signal.dat";
        MatrixXcd loaded_mat;
        if (!loadMatrix(oss.str(), loaded_mat)) return false;
        if (loaded_mat.cwiseAbs().maxCoeff() < 1e-15) return false;

        int rows = min((int)loaded_mat.rows(), _nrn);
        int cols = min((int)loaded_mat.cols(), _nan1);
        Radar_Sig.block(0, 0, rows, cols) = loaded_mat.block(0, 0, rows, cols);

        cout << "  [数据] 已加载: " << oss.str()
             << " (" << loaded_mat.rows() << "x" << loaded_mat.cols()
             << ", max=" << loaded_mat.cwiseAbs().maxCoeff() << ")" << endl;

        // 加载跳频序列f (Case1和Case5需要)
        if (mode == 1 || mode == 5) {
            ostringstream oss_f;
            oss_f << "01_waveform_Case" << mode << "_跳频序列_freq_hop.dat";
            VectorXd loaded_f;
            if (loadVector(oss_f.str(), loaded_f)) {
                int len = min((int)loaded_f.size(), _nan1);
                for (int i = 0; i < len; ++i) f(i) = loaded_f(i);
                cout << "  [数据] 已加载跳频序列: " << oss_f.str()
                     << " (" << loaded_f.size() << "点)" << endl;
            } else {
                cout << "  [警告] 未找到跳频序列: " << oss_f.str()
                     << " (Case" << mode << "处理可能不准确)" << endl;
            }
        }

        // 加载随机相位序列phi1 (Case2和Case5需要)
        if (mode == 2 || mode == 5) {
            ostringstream oss_p;
            oss_p << "01_waveform_Case" << mode << "_随机相位序列_random_phase.dat";
            VectorXcd loaded_phi;
            if (loadVector(oss_p.str(), loaded_phi)) {
                int len = min((int)loaded_phi.size(), _nan1);
                for (int i = 0; i < len; ++i) phi1(i) = loaded_phi(i);
                cout << "  [数据] 已加载随机相位序列: " << oss_p.str()
                     << " (" << loaded_phi.size() << "点)" << endl;
            } else {
                cout << "  [警告] 未找到随机相位序列: " << oss_p.str()
                     << " (Case" << mode << "处理可能不准确)" << endl;
            }
        }

        // 加载等效载频序列并写入全局f (Case4需要用于二次相位补偿)
        if (mode == 4) {
            ostringstream oss_freq;
            oss_freq << "01_waveform_Case" << mode << "_等效载频序列_freq_seq.dat";
            VectorXd loaded_freq;
            if (loadVector(oss_freq.str(), loaded_freq)) {
                int len = min((int)loaded_freq.size(), _nan1);
                for (int i = 0; i < len; ++i) f(i) = loaded_freq(i);
                cout << "  [数据] 已加载等效载频序列到全局f: " << oss_freq.str()
                     << " (" << loaded_freq.size() << "点, max=" << loaded_freq.cwiseAbs().maxCoeff() << " Hz)" << endl;
            } else {
                cout << "  [警告] 未找到等效载频序列: " << oss_freq.str()
                     << " (Case4将使用线性步进载频)" << endl;
            }
        }

        return true;
    }
    else if (mode == 6) {
        // ── Case6: 从模块02加载含干扰回波 ──
        // 优先Case4~10(通常有非零数据),再Case1~3
        int try_order[] = {4, 5, 6, 7, 8, 9, 10, 1, 2, 3};
        for (int jamCase : try_order) {
            ostringstream oss;
            oss << "02_jamming_Case" << jamCase << "_含干扰回波_jammed.dat";
            MatrixXcd loaded_mat;
            if (loadMatrix(oss.str(), loaded_mat)) {
                double loaded_norm = loaded_mat.cwiseAbs().maxCoeff();
                if (loaded_norm < 1e-15) continue;
                int rows = min((int)loaded_mat.rows(), _nrn);
                int cols = min((int)loaded_mat.cols(), _nan1);
                Radar_Sig.block(0, 0, rows, cols) = loaded_mat.block(0, 0, rows, cols);
                cout << "  [数据] 已加载: " << oss.str()
                     << " (" << loaded_mat.rows() << "x" << loaded_mat.cols()
                     << ", max=" << loaded_norm << ")" << endl;
                return true;
            }
        }

        // ── 兜底: 使用EchoGenerator4生成含干扰回波 ──
        cout << "  [兜底] 未找到模块02数据,使用EchoGenerator4生成..." << endl;
        const char* jam_names[] = {"", "ISDJ间歇直接转发", "ISRJ间歇重复转发",
                                   "ISCJ间歇循环转发", "NBJ窄带瞄频噪声", "RDJ距离欺骗"};
        const int jam_order[] = {4, 5, 1, 2, 3};
        for (int jamType : jam_order) {
            cout << "  [兜底] 尝试生成含" << jam_names[jamType] << "干扰的回波..." << endl;
            EchoGeneratorResult4 result = generateEcho4(_nan1, jamType);
            double echo_norm = result.echo_signal.cwiseAbs().maxCoeff();
            if (echo_norm < 1e-15) continue;

            int rows = min((int)result.echo_signal.rows(), _nan1);
            int cols = min((int)result.echo_signal.cols(), _nrn);
            for (int num = 0; num < rows; ++num)
                for (int k = 0; k < cols; ++k)
                    Radar_Sig(k, num) = result.echo_signal(num, k);

            cout << "  [兜底] 已生成含" << jam_names[jamType] << "干扰的回波"
                 << " (max=" << echo_norm << ")" << endl;
            return true;
        }

        // 最后兜底: 纯LFM
        cout << "  [兜底] 所有干扰类型均失败,生成纯LFM目标回波" << endl;
        const double c_val = 3e8;
        const double _fc = fc(), _Tp = Tp(), _gama = gama();
        const double _fs = fs(), _Rs = Rs(), _Vr = Vr(), _prf = prf();
        for (int k = 0; k < _nan1; ++k) {
            double R = _Rs + _Vr * k / _prf;
            for (int i = 0; i < _nrn; ++i) {
                double t = tnrn(i);
                double tau = t - 2.0 * R / c_val;
                if (tau >= -_Tp / 2.0 && tau <= _Tp / 2.0) {
                    complex<double> ph1 = exp(complex<double>(0, PI * _gama * tau * tau));
                    double carrier_phase = 2.0 * PI * (_fc / _fs) * i;
                    complex<double> base = precise_expj_2pi_scalar(_fc * tnrn(0));
                    complex<double> carrier = exp(complex<double>(0, carrier_phase)) * base;
                    Radar_Sig(i, k) = ph1 * carrier;
                }
            }
        }
        return true;
    }

    return false;
}

int chuli() {
    init_tnrn_and_fr(tnrn, fr);
    win = (fr.array().abs() <= B() / 2.0).cast<double>();

    const char* modeNames[] = {"", "跳频信号处理(速度补偿+解模糊)", "固定载频信号处理(随机相位补偿)",
        "传统脉冲压缩+速度估计(解模糊)", "改进型脉冲压缩(二次相位补偿+速度解模糊)",
        "复合处理(跳频+随机相位联合补偿)", "时频干扰解耦(Tsallis交叉熵)"};

    for (int mode = 1; mode <= 6; ++mode) {
        cout << "\n========== 处理模式" << mode << ": " << modeNames[mode] << " ==========" << endl;
        chuli_mode = mode;
        F = MatrixXcd::Zero(nrn(), nan1());

        // 为当前Case加载匹配数据
        bool loaded = load_case_data(mode);
        if (!loaded) {
            cout << "  [跳过] 无匹配输入数据,处理模式" << mode << "未执行" << endl;
            cout << "处理模式" << mode << " 跳过" << endl;
            continue;
        }

        // 打印数据诊断
        double case_sig_max = Radar_Sig.cwiseAbs().maxCoeff();
        int case_nonzero = 0;
        for (int i = 0; i < Radar_Sig.size(); ++i)
            if (abs(*(Radar_Sig.data() + i)) > 1e-15) case_nonzero++;
        cout << "  [诊断] Radar_Sig: 最大幅值=" << case_sig_max
             << " 非零元素=" << case_nonzero << "/" << Radar_Sig.size() << endl;

        switch (mode) {
            case 1: chuli_Case1(Radar_Sig, F); break;
            case 2: chuli_Case2(Radar_Sig, F); break;
            case 3: chuli_Case3(Radar_Sig, F); break;
            case 4: chuli_Case4(Radar_Sig, F); break;
            case 5: chuli_Case5(Radar_Sig, F); break;
            case 6: chuli_Case6(Radar_Sig);   break;
        }

        cout << "处理模式" << mode << " 完成" << endl;
    }

    return 0;
}
