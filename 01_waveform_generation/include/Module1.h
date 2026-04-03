// 本模块包含了所有模式的波形生成函数内容(Case1~5)
#ifndef MODULE1_H
#define MODULE1_H
#include "Module0.h"          // 工具函数(FFT/IFFT/saveMatrix等) + 全局参数
#include "Config.h"           // 配置管理器(已通过Module0.h间接包含,此处显式引入确保可用)

#define CFG Config::instance()  // 配置管理器单例快捷宏(用于读取waveform.*参数)

int boxing();                 // 波形生成主函数:自动遍历Case1~5,打印参数+生成信号+保存结果

// ── 高精度辅助函数 ──
// 当 freq*t 或 freq*R/c 的数值很大时,直接计算 exp(j*2*PI*x) 会因双精度丢失小数部分而精度不足.
// 利用 exp(j*2*PI*x) = exp(j*2*PI*frac(x)) 只对 [0,1) 的小数部分计算 sin/cos,避免精度损失.

// 精确计算标量 exp(j*2*PI*x),x 可以为任意大小
inline complex<double> precise_expj_2pi_scalar(double x) {
    double frac = x - floor(x);   // 小数部分 [0, 1)
    double phase = 2.0 * PI * frac;
    return complex<double>(cos(phase), sin(phase));
}

// 精确计算向量 exp(j*2*PI*freq*t),其中 t 为 Eigen 数组
// 拆分为: exp(j*2*PI*freq*t) = exp(j*2*PI*(freq/fs)*n) * exp(j*2*PI*freq*Tstart)
//   - (freq/fs)*n: 数字载波,分子分母均为合理量级,不会溢出
//   - freq*Tstart: 标量,用 precise_expj_2pi_scalar 处理
inline ArrayXcd precise_expj_2pi_array(double freq, const ArrayXd& t_arr, double fs_val, double Tstart) {
    int N = t_arr.size();
    ArrayXd n = ArrayXd::LinSpaced(N, 0.0, N - 1.0);
    ArrayXd carrier_phase = 2.0 * PI * (freq / fs_val) * n;  // 数字载波相位,精确
    complex<double> base = precise_expj_2pi_scalar(freq * Tstart);  // 起始偏移,精确标量
    return (carrier_phase * I_complex).exp() * base;
}

/********************************************** 波形生成函数 开始 ************************************************/

// 模式1:固定跳频波形(每次脉冲随机选择固定频点)
inline void boxing_Case1(MatrixXcd& Radar_Sig) {
    double c = 3e8;
    int N = CFG.getInt("waveform.case1_freq_hop.N", 10);
    double delta_f = CFG.getDouble("waveform.case1_freq_hop.delta_f", B());
    ostringstream oss;
    oss << "跳频点数: " << N << "\n跳频点间隔: " << delta_f << " Hz\n";
    cout << oss.str();

    f = VectorXd::Zero(nan1());      // 初始化载频序列f
    VectorXd f_step(nan1());         // 频偏序列

    // 随机数生成器
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1, N); // 生成1~N的整数

    const double _fc = fc(), _Rs = Rs(), _Vr = Vr(), _prf = prf(), _Tp = Tp(), _gama = gama();
    const double _fs = fs(), _Tstart = Tstart();
    for (int k = 0; k < nan1(); ++k) {
        // 随机选择跳频频点
        int gg = dis(gen);
        f_step(k) = gg * delta_f;
        f(k) = _fc + f_step(k);

        // 计算目标距离(考虑平台运动)
        double R = _Rs + _Vr * k / _prf;

        // 生成距离门窗口
        win = ((tnrn.array() - 2 * R / c) >= -_Tp / 2).cast<double>()
            * ((tnrn.array() - 2 * R / c) <= _Tp / 2).cast<double>();

        // 生成射频信号(高精度:使用数字载波替代 freq*tnrn 以避免精度损失)
        ArrayXcd phase1 = (PI * _gama * (tnrn.array() - 2*R/c).square()).cast<complex<double>>();
        complex<double> ph_const = precise_expj_2pi_scalar(-2.0 * f(k) * R / c);  // -4*PI*f*R/c = 2*2*PI*(-f*R/c)
        ArrayXcd carrier = precise_expj_2pi_array(f(k), tnrn.array(), _fs, _Tstart);
        VectorXcd sig = win.array() * (phase1 * I_complex).exp() * ph_const * carrier;

        // 累加到信号矩阵
        Radar_Sig.col(k) += sig;
    }

    // 保存跳频序列 f: VectorXd, 维度 nan1×1, 每个脉冲的载频值(单位:Hz)
    saveVector("01_waveform_Case1_跳频序列_freq_hop.dat", f);
}




// 模式2:随机相位波形(固定载频,每个脉冲附加随机相位)
inline void boxing_Case2(MatrixXcd& Radar_Sig) {
    // 生成随机相位序列
    phi1 = VectorXcd::Zero(nan1());   // 初始化随机相位序列
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> dis(0, 1);

    for (int i = 0; i < nan1(); ++i) {
        double randomPhase = 2 * PI * dis(gen);
        phi1(i) = exp(I_complex * randomPhase);
    }

    const double _fc = fc(), _Rs = Rs(), _Vr = Vr(), _prf = prf(), _Tp = Tp(), _gama = gama();
    const double _fs = fs(), _Tstart = Tstart();
    for (int i = 0; i < nan1(); ++i) {
        double tm = static_cast<double>(i) / _prf;  // 当前脉冲时间
        double R = _Rs + _Vr * tm;                   // 目标实时距离

        // 距离门窗口
        win = ((tnrn.array() - 2 * R / c) >= -_Tp / 2).cast<double>()
            * ((tnrn.array() - 2 * R / c) <= _Tp / 2).cast<double>();

        // 基带信号(高精度:使用数字载波替代 _fc*tnrn 以避免精度损失)
        ArrayXcd phase1 = (PI * _gama * (tnrn.array() - 2*R/c).square()).cast<complex<double>>();
        complex<double> ph_const = precise_expj_2pi_scalar(-2.0 * _fc * R / c);
        ArrayXcd carrier = precise_expj_2pi_array(_fc, tnrn.array(), _fs, _Tstart);
        VectorXcd sig = win.array() * (phase1 * I_complex).exp() * ph_const * carrier * phi1(i);

        // 累加到信号矩阵
        Radar_Sig.col(i) += sig;
    }

    // 保存随机相位序列 phi1: VectorXcd, 维度 nan1×1, 每个脉冲的随机相位因子(exp(j*θ))
    saveVector("01_waveform_Case2_随机相位序列_random_phase.dat", phi1);
}





// 模式3:脉冲重复间隔抖动波形
inline void boxing_Case3(MatrixXcd& Radar_Sig) {
    double prt = CFG.getDouble("waveform.case3_pri_jitter.prt", 1000e-6);
    double amp = CFG.getDouble("waveform.case3_pri_jitter.amp", 1.0);
    int jitter_us = CFG.getInt("waveform.case3_pri_jitter.jitter_us", 20);
    ostringstream oss;
    oss << "信号幅度: " << amp << "\n脉冲重复间隔: " << prt/1e-6 << " μs\n抖动范围: ±" << jitter_us << " μs\n";
    cout << oss.str();

    VectorXd tm(nan1());
    for (int i = 0; i < nan1(); ++i) {
        tm(i) = i * prt;
    }

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(-jitter_us, jitter_us);
    VectorXd prt0(nan1());
    for (int i = 0; i < nan1(); ++i) {
        prt0(i) = prt + dis(gen) * 1e-6;
    }

    // 实际慢时间轴
    VectorXd tnan(nan1());
    tnan(0) = prt0(0);
    for (int i = 1; i < nan1(); ++i) {
        tnan(i) = tnan(i - 1) + prt0(i);
    }

    // 等效载频(补偿时间抖动)
    const double _fc = fc();
    VectorXd freq(nan1());
    freq(0) = 0;
    for (int i = 1; i < nan1(); ++i) {
        freq(i) = _fc * tnan(i) / tm(i);
    }


    const double _Rs = Rs(), _Vr = Vr(), _Tp = Tp(), _gama = gama();
    for (int k = 0; k < nan1(); ++k) {
        double R = _Rs - _Vr * tnan(k);  // 目标距离(考虑平台运动)

        // 距离门窗口
        win = ((tnrn.array() - 2 * R / c) >= -_Tp / 2).cast<double>()
            * ((tnrn.array() - 2 * R / c) <= _Tp / 2).cast<double>();

        // 基带信号(高精度:标量常相位 -4*PI*_fc*R/c 很大,用分数部分精确计算)
        ArrayXcd phase1 = (PI * _gama * (tnrn.array() - 2 * R / c).square()).cast<complex<double>>();
        complex<double> ph_const = precise_expj_2pi_scalar(-2.0 * _fc * R / c);
        VectorXcd sig = win.array() * amp * (phase1 * I_complex).exp() * ph_const;

        // 累加到信号矩阵
        Radar_Sig.col(k) += sig;
    }

    // 保存等效载频序列 freq: VectorXd, 维度 nan1×1, 补偿PRI抖动后的等效载频(单位:Hz)
    saveVector("01_waveform_Case3_等效载频序列_freq_seq.dat", freq);
}









// 模式4:混合波形(跳频+脉冲抖动)
inline void boxing_Case4(MatrixXcd& Radar_Sig) {
    double delta_f = CFG.getDouble("waveform.case4_hybrid.delta_f", B());
    int fcnum = CFG.getInt("waveform.case4_hybrid.fcnum", 16);
    int pulg_num = nan1() / fcnum;
    double amp = CFG.getDouble("waveform.case4_hybrid.amp", 1.0);
    double prt = CFG.getDouble("waveform.case4_hybrid.prt", 1000e-6);
    int jitter_us = CFG.getInt("waveform.case4_hybrid.jitter_us", 20);
    ostringstream oss;
    oss << "频率步进: " << delta_f << " Hz\n载频分组数: " << fcnum << "\n每组脉冲数: " << pulg_num
        << "\n信号幅度: " << amp << "\n脉冲重复间隔: " << prt/1e-6 << " μs\n抖动范围: ±" << jitter_us << " μs\n";
    cout << oss.str();

    VectorXd aa(nan1());

    random_device rd;
    mt19937 gen(rd());
    for (int i = 0; i < pulg_num; ++i) {
        vector<int> permutation(fcnum);
        iota(permutation.begin(), permutation.end(), 1);
        shuffle(permutation.begin(), permutation.end(), gen);
        for (int j = 0; j < fcnum; ++j) {
            aa(i * fcnum + j) = permutation[j] - floor(fcnum / 2);
        }
    }

    VectorXd freq(nan1());
    freq = (aa.array() * delta_f + fc()).matrix();

    VectorXd tm(nan1());
    for (int i = 0; i < nan1(); ++i) {
        tm(i) = i * prt;
    }

    uniform_int_distribution<> dis(-jitter_us, jitter_us);
    VectorXd prt0(nan1());
    for (int i = 0; i < nan1(); ++i) {
        prt0(i) = prt + dis(gen) * 1e-6;
    }

    VectorXd tnan(nan1());
    tnan(0) = prt0(0);
    for (int i = 1; i < nan1(); ++i) {
        tnan(i) = tnan(i - 1) + prt0(i);
    }

    VectorXd freq0(nan1());
    freq0(0) = 0;
    for (int i = 1; i < nan1(); ++i) {
        freq0(i) = freq(i) * tnan(i) / tm(i);
    }


    const double _Rs = Rs(), _Vr = Vr(), _Tp = Tp(), _gama = gama();
    for (int k = 0; k < nan1(); ++k) {
        double R = _Rs - _Vr * tnan(k);

        // 距离门窗口
        win = ((tnrn.array() - 2 * R / c) >= -_Tp / 2).cast<double>()
                       * ((tnrn.array() - 2 * R / c) <= _Tp / 2).cast<double>();

        // 基带信号(高精度:标量常相位 -4*PI*freq(k)*R/c 很大,用分数部分精确计算)
        ArrayXcd phase1 = (PI * _gama * (tnrn.array() - 2 * R / c).square()).cast<complex<double>>();
        complex<double> ph_const = precise_expj_2pi_scalar(-2.0 * freq(k) * R / c);
        VectorXcd sig = win.array() * amp * (phase1 * I_complex).exp() * ph_const;

        // 累加到信号矩阵
        Radar_Sig.col(k) += sig;
    }

    // 保存等效载频序列 freq0: VectorXd, 维度 nan1×1, 跳频+PRI抖动补偿后的等效载频(单位:Hz)
    saveVector("01_waveform_Case4_等效载频序列_freq_seq.dat", freq0);
}




// 模式5:跳频+随机相位复合波形
inline void boxing_Case5(MatrixXcd& Radar_Sig) {
    int N = CFG.getInt("waveform.case5_combined.N", 10);
    double delta_f = CFG.getDouble("waveform.case5_combined.delta_f", B());
    ostringstream oss;
    oss << "跳频点数: " << N << "\n频率步进: " << delta_f << " Hz\n";
    cout << oss.str();

    VectorXd f_step(nan1());         // 频偏序列
    f = VectorXd::Zero(nan1());      // 初始化载频序列f
    phi1 = VectorXcd::Zero(nan1());  // 初始化随机相位序列

    // 随机数生成器
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis_gg(1, N);  // 用于随机选频
    uniform_real_distribution<> dis_phi(0, 1); // 用于生成随机相位

    // 生成随机相位序列
    for (int i = 0; i < nan1(); ++i) {
        double randomPhase = 2 * PI * dis_phi(gen);
        phi1(i) = exp(complex<double>(0, randomPhase));
    }

    const double _fc = fc(), _Rs = Rs(), _Vr = Vr(), _prf = prf(), _Tp = Tp(), _gama = gama();
    const double _fs = fs(), _Tstart = Tstart();
    for (int k = 0; k < nan1(); ++k) {
        // 随机选频
        int gg = dis_gg(gen);
        f_step(k) = gg * delta_f;
        f(k) = _fc + f_step(k);  // 当前载频

        double R = _Rs + _Vr * k / _prf;  // 目标距离

        // 距离门窗口
        win = ((tnrn.array() - 2 * R / c) >= -_Tp / 2).cast<double>()
                       * ((tnrn.array() - 2 * R / c) <= _Tp / 2).cast<double>();

        // 射频信号(含跳频和随机相位,高精度:使用数字载波替代 freq*tnrn)
        ArrayXcd phase1 = (PI * _gama * (tnrn.array() - 2 * R / c).square()).cast<complex<double>>();
        complex<double> ph_const = precise_expj_2pi_scalar(-2.0 * f(k) * R / c);
        ArrayXcd carrier = precise_expj_2pi_array(f(k), tnrn.array(), _fs, _Tstart);
        VectorXcd sig = win.array() * (phase1 * I_complex).exp() * ph_const * carrier * phi1(k);

        // 累加到信号矩阵
        Radar_Sig.col(k) += sig;
    }

    // 保存跳频序列 f: VectorXd, 维度 nan1×1, 每个脉冲的载频值(单位:Hz)
    // 保存随机相位序列 phi1: VectorXcd, 维度 nan1×1, 每个脉冲的随机相位因子(exp(j*θ))
    saveVector("01_waveform_Case5_跳频序列_freq_hop.dat", f);
    saveVector("01_waveform_Case5_随机相位序列_random_phase.dat", phi1);
}

/********************************************** 波形生成函数 结束 ************************************************/

/*
*  模式 1:固定跳频波形
*    在该模式下,首先设定跳频点数N和频率步进量delta_f.接着,针对每个脉冲,从N个频点中随机选取一个,以此算出频偏和当前脉冲的载
*  频.同时考虑平台运动,计算当前脉冲对应的目标距离,再生成距离门选择窗口,此窗口用于筛选出脉宽内的信号.之后,生成包含线性调频信
*  号、距离延迟相位和载频调制的射频信号,并累加到信号矩阵中.最后,跳频序列会被保存下来.
*    其原理是通过在不同脉冲间随机改变载频,增加信号的复杂性和抗干扰能力.线性调频信号可提高距离分辨率,距离延迟相位反映目标位置,
*  载频调制使信号能在特定频率传输.
*    注意事项方面,跳频点数N和频率步进量delta_f的选择很关键,要保证随机选频足够随机,且准确考虑平台运动对目标距离的影响,否则会
*  影响信号性能和处理准确性.

*  模式 2:随机相位波形
*    此模式先产生一个长度与脉冲数相同的随机相位序列.对于每个脉冲,先算出当前脉冲时间和目标实时距离,再生成距离门窗口.随后生成
*  包含线性调频信号、距离延迟相位、载频调制以及附加随机相位的基带信号,并累加到信号矩阵中,最后保存随机相位序列.
*    原理是保持载频不变,通过为每个脉冲附加随机相位增加信号随机性,使信号在时域和频域更难被预测和干扰,同时线性调频信号和距离延
*  迟相位保证基本特性和目标定位能力.
*    要注意随机相位序列生成需有良好随机性和统计特性,载频要稳定,信号处理时要正确处理随机相位影响,避免信号失真.

*  模式 3:脉冲重复间隔抖动波形
*    先设定标称脉冲重复间隔,生成标称慢时间轴.然后为每个脉冲的重复间隔加入随机抖动,得到实际慢时间轴并计算等效载频.对于每个脉冲,
*  计算目标距离,生成距离门窗口,接着生成包含线性调频信号和距离相位的基带信号,累加到信号矩阵中.
*    其原理是通过随机改变脉冲之间的时间间隔,使敌方难以捕捉信号规律,增加抗干扰能力,线性调频信号和距离相位用于目标检测和定位.
*    需注意随机抖动范围和分布要合理,等效载频计算要准确,信号处理时要考虑脉冲重复间隔抖动对相关算法的影响.

*  模式 4:混合波形(跳频 + 脉冲抖动)
*    先设置频率步进和载频分组数,在每组内随机排列频偏索引,生成实际载频序列.同时进行类似模式 3 的时间参数设置,得到实际慢时间轴和
*  等效载频.对于每个脉冲,计算目标距离,生成距离门窗口,使用跳频后的载频生成基带信号,累加到信号矩阵中.
*    该模式结合了跳频和脉冲抖动的优点,通过跳频改变载频、脉冲抖动改变脉冲时间间隔,增强信号复杂性和抗干扰能力.
*    要注意跳频序列和脉冲抖动设计需相互配合,信号处理复杂度增加需采用高效算法,且保证跳频和脉冲抖动的随机性与稳定性.

*  模式 5:跳频 + 随机相位复合波形
*    先设定跳频点数和频率步进量,生成随机相位序列.对于每个脉冲,随机选跳频频点,计算频偏和载频,考虑平台运动计算目标距离,生成距
*  离门窗口.之后生成包含线性调频信号、距离延迟相位、载频调制和附加随机相位的射频信号,累加到信号矩阵中,最后保存跳频序列和随机相位
*    序列.
*    原理是结合跳频和随机相位特性,使信号抗干扰能力更强、被截获概率更低,线性调频信号和距离延迟相位保证基本性能.
*    注意跳频和随机相位组合要合理,两者生成相互独立,信号处理时要充分考虑其对算法的影响并进行优化. 
*/

#endif // MODULE1_H
