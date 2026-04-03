/*
 * 模块04: 干扰识别 + 信号处理 (signal_processing)
 *
 * 功能: 先对雷达信号进行干扰类型识别,再执行6种信号处理模式
 *       两个子模块串行执行:
 *       1) shibie() - 干扰识别: 基于STFT时频分析识别10种干扰类型
 *       2) chuli()  - 信号处理: 每个Case独立加载匹配数据后执行
 *
 * 数据流设计:
 *   shibie() — 加载模块02含干扰回波(或内部兜底生成),用于干扰类型识别
 *   chuli()  — 每个Case独立加载匹配数据:
 *     Case1 ← 01_Case1(跳频波形 + 跳频序列f)
 *     Case2 ← 01_Case2(随机相位波形 + 相位序列phi1)
 *     Case3 ← 01_Case3(常规LFM波形)
 *     Case4 ← 01_Case4(NLFM波形)
 *     Case5 ← 01_Case5(复合波形 + f + phi1)
 *     Case6 ← 02_CaseN(含干扰回波,或内部兜底)
 *
 * 依赖: Module0.h, Module2.5.h, Module3.h, EchoGenerator4.h, Config.h
 */
#include "Module0.h"        // 工具函数 + 全局参数
#include "Module2.5.h"      // 干扰识别函数(shibie)
#include "Module3.h"        // 信号处理函数(chuli)
#include "EchoGenerator4.h" // 兜底信号生成器
#include "Config.h"         // 配置管理器

// ── 兜底信号生成: 使用EchoGenerator4 ──
// 仅在无法从模块02加载含干扰回波时调用
static bool generate_fallback_signal() {
    const int _nan1 = nan1();
    const char* jam_names[] = {"", "ISDJ间歇直接转发", "ISRJ间歇重复转发",
                               "ISCJ间歇循环转发", "NBJ窄带瞄频噪声", "RDJ距离欺骗"};
    const int try_order[] = {4, 5, 1, 2, 3};

    for (int jamType : try_order) {
        cout << "[兜底] 尝试生成含" << jam_names[jamType] << "干扰的回波 (jamType=" << jamType << ")..." << endl;
        EchoGeneratorResult4 result = generateEcho4(_nan1, jamType);
        double echo_norm = result.echo_signal.cwiseAbs().maxCoeff();
        if (echo_norm < 1e-15) continue;

        int rows = min((int)result.echo_signal.rows(), _nan1);
        int cols = min((int)result.echo_signal.cols(), nrn());
        Radar_Sig.setZero();
        for (int num = 0; num < rows; ++num)
            for (int k = 0; k < cols; ++k)
                Radar_Sig(k, num) = result.echo_signal(num, k);

        cout << "[兜底] 已生成含" << jam_names[jamType] << "干扰的回波信号"
             << " (max=" << echo_norm << ")" << endl;
        return true;
    }

    // 最后兜底: 纯LFM目标回波
    cout << "[兜底] 所有干扰类型均失败,生成纯LFM目标回波" << endl;
    const double c_val = 3e8;
    const double _fc = fc(), _Tp = Tp(), _gama = gama();
    const double _fs = fs(), _Rs = Rs(), _Vr = Vr(), _prf = prf();
    const int _nrn = nrn();
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

// ── 为干扰识别加载含干扰数据 ──
// 优先从模块02加载,其次内部兜底生成
static bool load_jammed_data_for_shibie() {
    const int _nan1 = nan1();
    const int _nrn = nrn();

    // 尝试模块02含干扰回波,优先Case4~10(通常有数据),再Case1~3
    int try_order[] = {4, 5, 6, 7, 8, 9, 10, 1, 2, 3};
    for (int mode : try_order) {
        ostringstream oss;
        oss << "02_jamming_Case" << mode << "_含干扰回波_jammed.dat";
        MatrixXcd loaded_mat;
        if (loadMatrix(oss.str(), loaded_mat)) {
            double loaded_norm = loaded_mat.cwiseAbs().maxCoeff();
            if (loaded_norm < 1e-15) continue;
            int rows = min((int)loaded_mat.rows(), _nrn);
            int cols = min((int)loaded_mat.cols(), _nan1);
            Radar_Sig.setZero();
            Radar_Sig.block(0, 0, rows, cols) = loaded_mat.block(0, 0, rows, cols);
            cout << "[识别] 从模块02加载含干扰回波: " << oss.str()
                 << " (" << loaded_mat.rows() << "x" << loaded_mat.cols() << ", max=" << loaded_norm << ")" << endl;
            return true;
        }
    }
    return false;
}

int main() {
    Config::instance().load();
    init_tnrn_and_fr(tnrn, fr);

    // 初始化全局信号矩阵
    Radar_Sig = MatrixXcd::Zero(nrn(), nan1());
    f = VectorXd::Zero(nan1());
    phi1 = VectorXcd::Ones(nan1());

    // ── 加载含干扰数据用于干扰识别 ──
    if (!load_jammed_data_for_shibie()) {
        generate_fallback_signal();
    }

    // ── 打印诊断信息 ──
    double sig_norm = Radar_Sig.cwiseAbs().matrix().norm();
    double sig_max = Radar_Sig.cwiseAbs().maxCoeff();
    int nonzero_count = 0;
    for (int i = 0; i < Radar_Sig.size(); ++i)
        if (abs(*(Radar_Sig.data() + i)) > 1e-15) nonzero_count++;
    cout << "[诊断] Radar_Sig: Frobenius范数=" << sig_norm
         << " 最大幅值=" << sig_max
         << " 非零元素=" << nonzero_count << "/" << Radar_Sig.size() << endl;

    // ── 干扰识别(基于含干扰数据) ──
    shibie();

    // ── 信号处理(每个Case独立加载匹配数据) ──
    chuli();

    promptToExit();
    return 0;
}
