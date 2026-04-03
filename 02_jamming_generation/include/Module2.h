// 本模块包含了所有模式的干扰生成函数内容(Case1~10)
#ifndef MODULE2_H
#define MODULE2_H
#include "Module0.h"          // 工具函数(FFT/IFFT/saveMatrix/awgn等) + 全局参数
#include "Config.h"           // 配置管理器(已通过Module0.h间接包含,此处显式引入确保可用)

#define CFG Config::instance()  // 配置管理器单例快捷宏(用于读取jamming.*参数)

int ganrao();                 // 干扰生成主函数:自动遍历Case1~10,打印参数+生成干扰+保存结果

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
inline ArrayXcd precise_expj_2pi_array(double freq, const ArrayXd& t_arr, double fs_val, double Tstart_val) {
    int N = t_arr.size();
    ArrayXd n = ArrayXd::LinSpaced(N, 0.0, N - 1.0);
    ArrayXd carrier_phase = 2.0 * PI * (freq / fs_val) * n;  // 数字载波相位,精确
    complex<double> base = precise_expj_2pi_scalar(freq * Tstart_val);  // 起始偏移,精确标量
    return (carrier_phase * I_complex).exp() * base;
}

/********************************************** 干扰生成函数 开始 ************************************************/

// 模式1:添加距离假目标干扰(RDJ)
inline void ganrao_Case1(const MatrixXcd& Radar_Sig, MatrixXcd& echo_target, MatrixXcd& RDJ_Sig)   
{
    RDJ_Sig = MatrixXcd::Zero(nrn(), nan1());
    echo_target = MatrixXcd::Zero(nrn(), nan1());

    int jj = CFG.getInt("jamming.case1_rdj.jj", 1);
    double Rj = CFG.getDouble("jamming.case1_rdj.Rj", 100.0);
    double t0 = 2 * Rj / c;
    double amp_j = CFG.getDouble("jamming.case1_rdj.amp_j", 10.0);

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "干扰脉冲延迟数 (jj): " << jj << "\n"
        << "干扰目标距离 (Rj): " << Rj << " m\n"
        << "干扰目标时延 (t0): " << t0 << " s\n"
        << "干扰信号幅度增益 (amp_j): " << amp_j << " dB\n";
    cout << oss.str();       // 一次性打印所有参数

    // ── 内部生成目标回波(转发型干扰需要截获真实信号作为转发源) ──
    const int _nan1 = nan1();
    const int _nrn = nrn();
    MatrixXcd target_echo = MatrixXcd::Zero(_nrn, _nan1);
    for (int k = 0; k < _nan1; ++k) {
        double Rt = Rs() - Vr() * k / prf();
        VectorXd time_diff = tnrn.array() - 2 * Rt / c;
        VectorXcd phase = (I_complex * PI * gama() * time_diff.array().square()).exp()
                        * precise_expj_2pi_scalar(-2.0 * Rt / lambda());
        Array<bool, -1, 1> wint = (time_diff.array() >= -Tp()/2.0) && (time_diff.array() <= Tp()/2.0);
        target_echo.col(k) = wint.cast<double>() * phase.array();
    }

    // 前 jj 个脉冲保持目标回波信号(无干扰)
    for (int k = 0; k < jj; ++k) {
        echo_target.col(k) = target_echo.col(k);
    }

    // 从第 jj+1 个脉冲开始,截获目标回波并叠加距离假目标干扰
    for (int k = jj; k < _nan1; ++k) {
        VectorXcd sig = target_echo.col(k - jj);   // 截获前向脉冲的目标回波

        // FFT
        sig = fft(sig);

        // 频域引入时延相位项(exp(-j2πfΔt))
        for (int i = 0; i < _nrn; ++i) {
            complex<double> phase = precise_expj_2pi_scalar(-fr(i) * t0);
            sig(i) *= phase;
        }

        // IFFT
        sig = ifft(sig);

        // 累加干扰信号
        RDJ_Sig.col(k) += sig;

        // 目标回波与加权干扰信号叠加
        echo_target.col(k) = target_echo.col(k) + amp_j * RDJ_Sig.col(k);
    }
    // 生成最终回波信号
    // RDJ_Sig: MatrixXcd, 维度 nrn×nan1, 距离假目标干扰信号(复数,纯干扰不含原始信号)
    saveMatrix("02_jamming_Case1_距离假目标干扰_RDJ.dat",RDJ_Sig);

}



// 模式2:添加速度假目标干扰(VDJ)
inline void ganrao_Case2(const MatrixXcd& Radar_Sig,MatrixXcd& echo_target,MatrixXcd& VDJ_Sig)   
{
    double Vj = CFG.getDouble("jamming.case2_vdj.Vj", 1e5);
    int jj = CFG.getInt("jamming.case2_vdj.jj", 1);
    double Rj = CFG.getDouble("jamming.case2_vdj.Rj", 10.0);
    double t0 = 2 * Rj / c;
    double amp_j = CFG.getDouble("jamming.case2_vdj.amp_j", 10.0);
    double fj = 2 * Vj / lambda();

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "假目标速度 (Vj): " << Vj << " m/s\n"
        << "干扰延迟脉冲数 (jj): " << jj << "\n"
        << "假目标距离 (Rj): " << Rj << " m\n"
        << "假目标时延 (t0): " << t0 << " s\n"
        << "干扰信号幅度增益 (amp_j): " << amp_j << " dB\n"
        << "假目标多普勒频率 (fj): " << fj << " Hz\n";
    cout << oss.str();       // 一次性打印所有参数
    
    // ── 内部生成目标回波(转发型干扰需要截获真实信号作为转发源) ──
    const int _nan1 = nan1();
    const int _nrn = nrn();
    MatrixXcd target_echo = MatrixXcd::Zero(_nrn, _nan1);
    for (int k = 0; k < _nan1; ++k) {
        double Rt = Rs() - Vr() * k / prf();
        VectorXd time_diff = tnrn.array() - 2 * Rt / c;
        VectorXcd phase = (I_complex * PI * gama() * time_diff.array().square()).exp()
                        * precise_expj_2pi_scalar(-2.0 * Rt / lambda());
        Array<bool, -1, 1> wint = (time_diff.array() >= -Tp()/2.0) && (time_diff.array() <= Tp()/2.0);
        target_echo.col(k) = wint.cast<double>() * phase.array();
    }

    VDJ_Sig = MatrixXcd::Zero(_nrn, _nan1);
    echo_target = MatrixXcd::Zero(_nrn, _nan1);

    // 前jj个脉冲保持目标回波信号(无干扰)
    for (int k = 0; k < jj; ++k) {
        echo_target.col(k) = target_echo.col(k);
    }
    
    // 从第jj+1个脉冲开始,截获目标回波并叠加速度假目标干扰
    for (int k = jj; k < _nan1; ++k) {
        VectorXcd sig = target_echo.col(k - jj);   // 截获前向脉冲的目标回波
        
        // 对信号施加多普勒调制:对每个距离采样点,乘以相应的调制因子 exp(j*2π*f_j*tnrn(i))
        for (int i = 0; i < _nrn; ++i) {
            // 计算多普勒调制相位因子(高精度:避免 fj*tnrn 较大时精度丢失)
            complex<double> doppler_mod = precise_expj_2pi_scalar(fj * tnrn(i));
            sig(i) *= doppler_mod;
        }

        sig = fft(sig);   // 对多普勒调制后的信号进行FFT变换至频域
        
        // 在频域中引入时延相位项:对每个频率采样点,乘以相位因子 exp(-j*2π*fr(i)*t0)
        for (int i = 0; i < _nrn; ++i) {
            complex<double> phase = precise_expj_2pi_scalar(-fr(i) * t0);
            sig(i) *= phase;
        }

        sig = ifft(sig);   // 对信号进行IFFT变换回到时域,生成干扰信号

        VDJ_Sig.col(k) += sig;   // 累加生成的干扰信号到VDJ干扰信号矩阵中
        
        // 叠加原始信号与加权后的干扰信号,形成最终的回波信号
        echo_target.col(k) = target_echo.col(k) + amp_j * VDJ_Sig.col(k);
    }
    // VDJ_Sig: MatrixXcd, 维度 nrn×nan1, 速度假目标干扰信号(复数,含多普勒调制)
    saveMatrix("02_jamming_Case2_速度假目标干扰_VDJ.dat",VDJ_Sig);
}



// 模式3:添加间歇采样转发干扰(ISRJ)
inline void ganrao_Case3(const MatrixXcd& Radar_Sig,MatrixXcd& echo_target,MatrixXcd& ISRJ_Sig)   
{
    ISRJ_Sig = MatrixXcd::Zero(nrn(), nan1());  // 初始化 ISRJ 干扰信号矩阵 ISRJ_Sig 用于存储生成的间歇采样转发干扰信号

    // ── 内部生成目标回波(转发型干扰需要截获真实信号作为转发源) ──
    const int _nan1 = nan1();
    const int _nrn = nrn();
    MatrixXcd target_echo = MatrixXcd::Zero(_nrn, _nan1);
    for (int k = 0; k < _nan1; ++k) {
        double Rt = Rs() - Vr() * k / prf();
        VectorXd time_diff = tnrn.array() - 2 * Rt / c;
        VectorXcd phase = (I_complex * PI * gama() * time_diff.array().square()).exp()
                        * precise_expj_2pi_scalar(-2.0 * Rt / lambda());
        Array<bool, -1, 1> wint = (time_diff.array() >= -Tp()/2.0) && (time_diff.array() <= Tp()/2.0);
        target_echo.col(k) = wint.cast<double>() * phase.array();
    }

    double Ts_ISRJ = CFG.getDouble("jamming.case3_isrj.Ts_ISRJ", 4e-6);
    double T_ISRJ = CFG.getDouble("jamming.case3_isrj.T_ISRJ", Ts_ISRJ / 4.0);
    double Rj = CFG.getDouble("jamming.case3_isrj.Rj", 10.0);
    double t0 = 2 * Rj / c;
    double amp_j = CFG.getDouble("jamming.case3_isrj.amp_j", 10.0);
    int Ts_Num = static_cast<int>(Ts_ISRJ * fs());           // 采样脉宽对应的采样点数,等于 T_ISRJ * fs
    int T_Num = static_cast<int>(T_ISRJ * fs());             // 采样周期对应的采样点数,等于 T_ISRJ * fs
    int N_ISRJ = static_cast<int>(Tp() / Ts_ISRJ);           // 采样周期内的采样次数,等于 Tp / Ts_ISRJ
    int Num_C_I = static_cast<int>(Ts_ISRJ / T_ISRJ) - 1;  // 切片重组次数,等于 Ts_ISRJ / T_ISRJ - 1

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "采样周期 (Ts_ISRJ): " << Ts_ISRJ << " s\n"
        << "采样脉宽 (T_ISRJ): " << T_ISRJ << " s\n"
        << "干扰目标距离 (Rj): " << Rj << " m\n"
        << "干扰目标时延 (t0): " << t0 << " s\n"
        << "干扰信号幅度增益 (amp_j): " << amp_j << "\n"
        << "采样脉宽对应的采样点数 (Ts_Num): " << Ts_Num << "\n"
        << "采样周期对应的采样点数 (T_Num): " << T_Num << "\n"
        << "采样周期内的采样次数 (N_ISRJ): " << N_ISRJ << "\n"
        << "切片重组次数 (Num_C_I): " << Num_C_I << "\n";
    cout << oss.str();        // 一次性打印所有参数

    // 对每个脉冲执行 ISRJ 干扰处理
    for (int m = 0; m < _nan1; ++m) {
        win = VectorXd::Zero(_nrn);  // 初始化距离门窗口 win 为全零逻辑矩阵,用于存储当前脉冲信号的有效区域

        // 寻找当前脉冲信号的有效区域,找到第一个非零采样点的索引,作为有效区域的起始位置
        int start_index = -1;
        for (int i = 0; i < _nrn; ++i) {
            if (abs(target_echo(i, m)) > 1e-12) {    // 使用一个很小的阈值判断非零
                start_index = i;
                break;
            }
        }
        // 如果未找到有效区域,则跳过当前脉冲的干扰处理
        if (start_index == -1) {
            continue;
        }

        // 生成间歇采样窗口(周期性选通)
        // 对每个切片 Ni,从 0 到 N_ISRJ-1,为对应的采样区间赋值为 1
        // 区间计算:从 start_index + Ni*Ts_Num 到 start_index + T_Num + Ni*Ts_Num
        for (int Ni = 0; Ni < N_ISRJ; ++Ni) {
            int idx_start = start_index + Ni * Ts_Num;
            int idx_end = start_index + T_Num + Ni * Ts_Num; // 注意:索引 idx_end 在 C++ 中为不包含上界
            for (int idx = idx_start; idx < idx_end && idx < _nrn; ++idx) {
                win(idx) = 1.0;
            }
        }

        // 截取当前脉冲 m 的有效采样段信号
        VectorXcd sig(_nrn);
        for (int i = 0; i < _nrn; ++i) {
            sig(i) = target_echo(i, m) * win(i);  // 有效区域内信号保留,其他区域置零
        }

        // 进行多普勒切片重组
        // 对每个切片重组次数 Ni,从 0 到 Num_C_I-1,累计处理后的信号到 ISRJ_Sig 的第 m 列
        for (int Ni = 0; Ni < Num_C_I; ++Ni) {
            VectorXcd sig0 = fft(sig);      // 对截取信号 sig 进行 FFT 变换到频域,得到频域信号 sig0

            // 由于 MATLAB 循环从 1 开始,这里采用 (Ni+1)*T_ISRJ 
            double doppler_delay = (Ni + 1) * T_ISRJ;  // 当前切片的多普勒延时,单位为秒,这里表示为 (Ni+1)*T_ISRJ 

            // 在频域中引入多普勒频移:对每个频率采样点,乘以相位因子 exp(-j*2π*fr(i)*doppler_delay)
            for (int i = 0; i < _nrn; ++i) {
                complex<double> phase = precise_expj_2pi_scalar(-fr(i) * doppler_delay);
                sig0(i) *= phase;
            }

            sig0 = ifft(sig0);       // 对引入多普勒频移后的信号进行 IFFT 变换回到时域

            for (int i = 0; i < _nrn; ++i) {
                ISRJ_Sig(i, m) += sig0(i);   // 将当前切片的处理结果累加到 ISRJ_Sig 的第 m 列上
            }
        }

        // 对当前脉冲 m 的累计干扰信号整体引入距离时延
        // 先对 ISRJ_Sig.col(m) 进行 FFT 变换到频域
        VectorXcd temp = fft(ISRJ_Sig.col(m));
        // 对频域信号的每个采样点,乘以相位因子 exp(-j*2π*fr(i)*t0) 以引入距离时延
        for (int i = 0; i < _nrn; ++i) {
            complex<double> phase = precise_expj_2pi_scalar(-fr(i) * t0);
            temp(i) *= phase;
        }
        temp = ifft(temp);
        for (int i = 0; i < _nrn; ++i) {
            ISRJ_Sig(i, m) = temp(i);   // 将带有时延补偿的信号进行 IFFT 变换回到时域,更新 ISRJ_Sig 的第 m 列
        }
    }

    // 叠加干扰信号与原始雷达信号,生成最终回波信号
    echo_target = amp_j * ISRJ_Sig + target_echo;
    // ISRJ_Sig: MatrixXcd, 维度 nrn×nan1, 间歇采样转发干扰信号(复数,切片重组+多普勒调制)
    saveMatrix("02_jamming_Case3_间歇采样转发干扰_ISRJ.dat",ISRJ_Sig);

}



// 模式4:添加窄带噪声干扰(Narrowband Noise Jamming)
inline void ganrao_Case4(const MatrixXcd& Radar_Sig,MatrixXcd& echo_target,MatrixXcd& NNJ)   
{
    // fft_lvbo_z1 用于存储每个脉冲经过低通滤波后并处理高频分量的频域信号,大小为 (nrn x nan1)
    MatrixXcd fft_lvbo_z1 = MatrixXcd::Zero(nrn(), nan1());
    
    // 初始化随机数生成器,用于生成高斯白噪声  采用 random_device 作为随机数种子
    static default_random_engine generator((unsigned)random_device{}());
    // 均值为 0,标准差为 sigma.对于复噪声,总功率 20 dBW 对应的线性功率为 10^(20/10)=100,
    // 则实部和虚部分量的方差各为 50,标准差 sigma = sqrt(50)
    double power_dBW = CFG.getDouble("jamming.case4_nnj.power_dBW", 20.0);
    double power_linear = pow(10.0, power_dBW / 10.0);
    double sigma = sqrt(power_linear / 2.0);
    normal_distribution<double> normal_dist(0.0, 1.0);
    
    // 针对每个脉冲生成噪声干扰并处理
    const int _nan1 = nan1();
    const int _nrn = nrn();
    const double _fc = fc();
    const double _fs = fs();
    const double _Tstart = Tstart();
    for (int m = 0; m < _nan1; ++m)
    {
        // 生成复高斯白噪声 z
        // z 为长度为 L 的复数向量,每个样本的实部和虚部均从正态分布生成,
        // 然后乘以载波调制因子 exp(j*2π*fc*tnrn(i)) 实现相位调制
        // (高精度:使用数字载波替代直接计算 fc*tnrn 以避免精度损失)
        ArrayXcd carrier = precise_expj_2pi_array(_fc, tnrn.array(), _fs, _Tstart);
        VectorXcd z(_nrn);
        for (int i = 0; i < _nrn; ++i)
        {
            // 生成实部和虚部
            double real_part = normal_dist(generator) * sigma;
            double imag_part = normal_dist(generator) * sigma;
            // 生成复数噪声样本并调制
            z(i) = complex<double>(real_part, imag_part) * carrier(i);
        }
        
        // 对生成的噪声信号进行 FFT 分析(此步骤主要用于分析,结果不在后续处理中使用)
        VectorXcd fft_z = fft(z);
        
        // 设计 8 阶低通 Butterworth 滤波器
        // 截止频率设为 3/10(归一化截止频率,0.3 表示截止频率为 奈奎斯特 频率的 30%)
        vector<double> b, a;
        butter(CFG.getInt("jamming.case4_nnj.butter_order", 8),
                CFG.getDouble("jamming.case4_nnj.butter_cutoff", 0.3), b, a);
        
        // 对噪声信号 z 进行低通滤波,得到滤波后的信号 lvbo_z
        VectorXcd lvbo_z = filter(b, a, z);
        
        // 对滤波后信号 lvbo_z 进行 FFT 变换到频域
        VectorXcd fft_lvbo_z = fft(lvbo_z);
        
        // 替换高频分量
        // 找到 fft_lvbo_z 中幅值最小的元素的索引 minIndex
        int minIndex = 0;
        double minVal = abs(fft_lvbo_z(0));
        for (int i = 1; i < _nrn; ++i)
        {
            double mag = abs(fft_lvbo_z(i));
            if (mag < minVal)
            {
                minVal = mag;
                minIndex = i;
            }
        }
        // 将 fft_lvbo_z 的后半部分(从索引 L/2 到 L-1)全部替换为幅值最小的那个值
        for (int i = _nrn / 2; i < _nrn; ++i)
        {
            fft_lvbo_z(i) = fft_lvbo_z(minIndex);
        }
        
        // 将处理后的频域信号存入 fft_lvbo_z1 的第 m 列
        fft_lvbo_z1.col(m) = fft_lvbo_z;
    }
    
    // 频域信号逆 FFT 转换为时域信号
    // 对 fft_lvbo_z1 的每一列进行 IFFT,得到时域干扰信号 NNJ
    for (int m = 0; m < _nan1; ++m)
    {
        VectorXcd col = fft_lvbo_z1.col(m);   // 取出第 m 列的频域信号
        VectorXcd ifft_col = ifft(col);       // 对该列信号进行 IFFT 变换
        NNJ.col(m) = ifft_col;                // 保存时域信号 NNJ 的第 m 列
    }
    
    // 生成最终回波信号
    echo_target = NNJ + Radar_Sig;
    // NNJ: MatrixXcd, 维度 nrn×nan1, 窄带噪声干扰信号(复数,低通滤波+载波调制)
    saveMatrix("02_jamming_Case4_窄带噪声干扰_NNJ.dat",NNJ);

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "噪声功率 (power_linear): " << power_linear << " (线性功率)\n"
        << "噪声标准差 (sigma): " << sigma << "\n"
        << "载波频率 (fc): " << fc() << " Hz\n";
    cout << oss.str();      // 一次性打印所有参数

}

 



// 模式5:添加距离波门拖引干扰(Jamming Range Gate Pull-Off)
inline void ganrao_Case5(const MatrixXcd& Radar_Sig,MatrixXcd& echo_target,MatrixXcd& JRGPO_Sig) {
    // 干扰参数设置
    double Vj = CFG.getDouble("jamming.case5_rgpo.Vj", 340.0);
    double amp_target = CFG.getDouble("jamming.case5_rgpo.amp_target", 1.0);
    double amp_jammer = CFG.getDouble("jamming.case5_rgpo.amp_jammer", 1.4);
    int drag_stages = CFG.getInt("jamming.case5_rgpo.drag_stages", 4);
    double awgn_snr = CFG.getDouble("jamming.case5_rgpo.awgn_snr", 10.0);

    // 计算目标初始参数
    double phi0 = acos(z_R0() / Rs());      // 目标初始俯仰角
    double theta0 = 0.0;                // 目标初始方位角
    double x0 = Rs() * sin(phi0) * sin(theta0);   // 目标初始 X 坐标
    double y0 = Rs() * sin(phi0) * cos(theta0);   // 目标初始 Y 坐标
    double R0 = Vector3d(x0, y0, 0.0).norm();   // 目标初始距离

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "假目标拖引速度 (Vj): " << Vj << " m/s\n"
        << "目标幅度 (amp_target): " << amp_target << "\n"
        << "干扰幅度 (amp_jammer): " << amp_jammer << "\n"
        << "拖引阶段次数 (drag_stages): " << drag_stages << "\n"
        << "目标初始参数:\n"
        << "目标初始俯仰角 (phi0): " << phi0 << " rad\n"
        << "目标初始方位角 (theta0): " << theta0 << " rad\n"
        << "目标初始 X 坐标 (x0): " << x0 << " m\n"
        << "目标初始 Y 坐标 (y0): " << y0 << " m\n"
        << "目标初始距离 (R0): " << R0 << " m\n";
    cout << oss.str();        // 一次性打印所有参数


    // 初始化信号矩阵
    JRGPO_Sig = MatrixXcd::Zero(nrn(), nan1());    // 初始化 RGPO 干扰信号矩阵
    echo_target = MatrixXcd::Zero(nrn(), nan1());  // 初始化目标回波矩阵

    //主处理循环
    const int _nan1 = nan1();
    const double _Vr = Vr();
    const double _prf = prf();
    const double _gama = gama();
    const double _lambda = lambda();
    const double _Tp = Tp();
    const double _Rs = Rs();
    const int _nrn = nrn();
    for (int k = 0; k < _nan1; ++k) { // 遍历每个脉冲
        // 生成目标回波
        double Rt = R0 - _Vr * k / _prf * 4000.0; // 当前脉冲的目标距离 (调整为 0 基索引)
        VectorXd time_diff_target = tnrn.array() - 2 * Rt / c; // 目标回波时间差
        VectorXcd phase_target = (I_complex * PI * _gama * time_diff_target.array().square()).exp() // LFM 相位
                               * precise_expj_2pi_scalar(-2.0 * Rt / _lambda);   // 距离相位(高精度)
        Array<bool, -1, 1> wint = (time_diff_target.array() >= -_Tp / 2.0) && (time_diff_target.array() <= _Tp / 2.0); // 目标时间窗
        VectorXcd reT = wint.cast<double>() * amp_target * phase_target.array(); // 目标回波信号
        reT = awgn(reT, awgn_snr); // 为目标回波添加高斯白噪声

        // 干扰参数计算（分阶段处理）
        double Rj;
        if (k == 0) { // 第一个拖引阶段
            Rj = R0 - _Vr * k / _prf * 4000.0 - Vj * (k + 0.01) / _prf * 4000.0; // 稍微分离回波
        } else if (k < drag_stages) { // 第二至第四拖引阶段
            Rj = R0 - _Vr * k / _prf * 4000.0 - Vj * k / _prf * 4000.0;
        } else { // 其他阶段
            Rj = R0 - _Vr * k / _prf * 4000.0 + Vj * (k - drag_stages + 1) / _prf * 4000.0; // 拖引后调整
        }

        // 生成干扰信号
        VectorXd time_diff_jammer = tnrn.array() - 2 * Rj / c; // 干扰信号时间差
        VectorXcd phase_jammer = (I_complex * PI * _gama * time_diff_jammer.array().square()).exp() // LFM 相位
                               * precise_expj_2pi_scalar(2.0 * Rj / _lambda);   // 距离相位(高精度)
        Array<bool, -1, 1> winj = (time_diff_jammer.array() >= -_Tp / 2.0) && (time_diff_jammer.array() <= _Tp / 2.0); // 干扰时间窗
        VectorXcd reJ = winj.cast<double>() * amp_jammer * phase_jammer.array(); // 干扰回波信号
        reJ = awgn(reJ, awgn_snr);

        // 信号存储
        JRGPO_Sig.col(k) = reJ; // 将干扰信号存入矩阵
        echo_target.col(k) = Radar_Sig.col(k) + JRGPO_Sig.col(k); // 叠加干扰信号到雷达信号形成总回波
    }

    // JRGPO_Sig: MatrixXcd, 维度 nrn×nan1, 距离波门拖引干扰信号(复数,分阶段拖引)
    saveMatrix("02_jamming_Case5_距离波门拖引干扰_RGPO.dat", JRGPO_Sig);
}




// 模式6:添加速度波门拖引干扰(Jamming Velocity Gate Pull-Off)
inline void ganrao_Case6(const MatrixXcd& Radar_Sig, MatrixXcd& echo_target,MatrixXcd& JVGPO_Sig) 
 {
    // 干扰参数设置
    double Vj = CFG.getDouble("jamming.case6_vgpo.Vj", 1e4);
    double amp_target = CFG.getDouble("jamming.case6_vgpo.amp_target", 1.0);
    double amp_jammer = CFG.getDouble("jamming.case6_vgpo.amp_jammer", 1.4);
    int drag_stages = CFG.getInt("jamming.case6_vgpo.drag_stages", 4);
    double awgn_snr = CFG.getDouble("jamming.case6_vgpo.awgn_snr", 10.0);
    double fd = 2 * Vr() / lambda();
    // 计算目标初始参数
    double phi0 = acos(z_R0() / Rs());         // 目标初始俯仰角
    double theta0 = 0.0;                   // 目标初始方位角
    double x0 = Rs() * sin(phi0) * sin(theta0);    // 目标初始X坐标
    double y0 = Rs() * sin(phi0) * cos(theta0);    // 目标初始Y坐标
    double R0 = Vector3d(x0, y0, 0.0).norm();    // 目标初始距离

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "假目标速度 (Vj): " << Vj << " m/s\n"
        << "目标幅度 (amp_target): " << amp_target << "\n"
        << "干扰幅度 (amp_jammer): " << amp_jammer << "\n"
        << "拖引阶段次数 (drag_stages): " << drag_stages << "\n"
        << "目标多普勒频率 (fd): " << fd << " Hz\n"
        << "目标初始参数:\n"
        << "目标初始俯仰角 (phi0): " << phi0 << " rad\n"
        << "目标初始方位角 (theta0): " << theta0 << " rad\n"
        << "目标初始 X 坐标 (x0): " << x0 << " m\n"
        << "目标初始 Y 坐标 (y0): " << y0 << " m\n"
        << "目标初始距离 (R0): " << R0 << " m\n";
    cout << oss.str();   // 一次性打印所有参数


    JVGPO_Sig = MatrixXcd::Zero(nrn(), nan1());    // 初始化干扰信号
    echo_target = MatrixXcd::Zero(nrn(), nan1());  // 初始化目标回波

    // 主处理循环
    const int _nan1 = nan1();
    const double _Vr = Vr();
    const double _prf = prf();
    const double _gama = gama();
    const double _lambda = lambda();
    const double _Tp = Tp();
    const int _nrn = nrn();
    const double _fc = fc();
    for (int k = 0; k < _nan1; ++k) {
        double Rt = R0 + _Vr * (k+1) / _prf;            // 当前脉冲的距离

        // 生成目标回波
        VectorXd time_diff_target = tnrn.array() - 2*Rt/c;
        VectorXcd phase_target = (I_complex * PI * _gama * time_diff_target.array().square()).exp()     // LFM
                                * precise_expj_2pi_scalar(-2.0 * Rt / _lambda)      // 距离相位(高精度)
                                * (I_complex * 2.0 * PI * fd * time_diff_target.array()).exp();  // 多普勒速度相位
        Array<bool, -1, 1> wint = (time_diff_target.array() >= -_Tp/2) && (time_diff_target.array() <= _Tp/2);
        VectorXcd reT = wint.cast<double>()
                        * amp_target
                        * phase_target.array();   
        reT = awgn(reT, awgn_snr);

        // 干扰参数计算（分阶段处理）
        double fj;
        if (k == 0) { // 对应MATLAB的k <= 1
            fj = 2 * Vj / _lambda * (k + 0.001) / _prf * 10000 * 4;
        } else if (k < drag_stages) {           // 对应MATLAB的k <= drag_stages
            fj = 2 * Vj / _lambda * k / _prf * 10000 * 4;
        } else {            // 保持阶段
            fj = 2 * Vj / _lambda * (k - drag_stages + 1) / _prf * 10000 * 4;
        }

        // 生成干扰信号
        VectorXd time_diff_jammer = tnrn.array() - 2*Rt/c;
        VectorXcd phase_jammer = (I_complex * PI * _gama * time_diff_jammer.array().square()).exp()   // LFM
                               * precise_expj_2pi_scalar(-2.0 * Rt / _lambda)          // 距离相位(高精度)
                               * (I_complex * 2.0 * PI * (fd + fj) * time_diff_jammer.array()).exp();   // 多普勒速度相位
        Array<bool, -1, 1> winj = (time_diff_jammer.array() >= -_Tp/2) && (time_diff_jammer.array() <= _Tp/2);
        VectorXcd reJ = winj.cast<double>()
                        * amp_jammer
                        * phase_jammer.array();
        
        reJ = awgn(reJ, awgn_snr);

        JVGPO_Sig.col(k) = reJ;     // 累加各个脉冲的干扰信号到JVGPO_Sig
        echo_target.col(k) = Radar_Sig.col(k) + JVGPO_Sig.col(k);  // 叠加各个脉冲的干扰信号到Radar_Sig形成含干扰回波echo_target
    }

    // JVGPO_Sig: MatrixXcd, 维度 nrn×nan1, 速度波门拖引干扰信号(复数,含多普勒拖引)
    saveMatrix("02_jamming_Case6_速度波门拖引干扰_VGPO.dat", JVGPO_Sig);
}





// 模式7:密集复制转发假目标干扰(Dense Repeater False Target Jamming)
inline void ganrao_Case7(const MatrixXcd& Radar_Sig, MatrixXcd& echo_target, MatrixXcd& JMT_Sig) {
    // 干扰参数设置
    double JSR = CFG.getDouble("jamming.case7_drftj.JSR", 0.0);
    double amp_target = CFG.getDouble("jamming.case7_drftj.amp_target", 1.0);
    int num_jam = CFG.getInt("jamming.case7_drftj.num_jam", 50);
    double detaR = CFG.getDouble("jamming.case7_drftj.detaR", 50.0);
    double awgn_snr = CFG.getDouble("jamming.case7_drftj.awgn_snr", 10.0);
    double amp_jammer = amp_target * pow(10, JSR / 20.0);

    // 计算目标初始参数
    double phi0 = acos(z_R0() / Rs());      // 目标初始俯仰角
    double theta0 = 0.0;                // 目标初始方位角
    double x0 = Rs() * sin(phi0) * sin(theta0);  // 目标初始 X 坐标
    double y0 = Rs() * sin(phi0) * cos(theta0);  // 目标初始 Y 坐标
    double R0 = Vector3d(x0, y0, 0.0).norm();  // 目标初始距离

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "干扰信号增益 (JSR): " << JSR << " dB\n"
        << "目标幅度 (amp_target): " << amp_target << "\n"
        << "转发次数 (num_jam): " << num_jam << "\n"
        << "每次转发的距离增量 (detaR): " << detaR << " m\n"
        << "干扰幅度 (amp_jammer): " << amp_jammer << "\n"
        << "目标初始参数:\n"
        << "目标初始俯仰角 (phi0): " << phi0 << " rad\n"
        << "目标初始方位角 (theta0): " << theta0 << " rad\n"
        << "目标初始 X 坐标 (x0): " << x0 << " m\n"
        << "目标初始 Y 坐标 (y0): " << y0 << " m\n"
        << "目标初始距离 (R0): " << R0 << " m\n";
    cout << oss.str();  // 一次性打印所有参数

    JMT_Sig = MatrixXcd::Zero(nrn(), nan1());     // 初始化 MT 干扰信号矩阵
    echo_target = MatrixXcd::Zero(nrn(), nan1()); // 初始化目标回波矩阵
    MatrixXcd Unj_Sig = MatrixXcd::Zero(nrn(), nan1()); // 初始化未受干扰的回波信号矩阵

    // 主处理循环
    const int _nan1 = nan1();
    const double _Vr = Vr();
    const double _prf = prf();
    const double _gama = gama();
    const double _lambda = lambda();
    const double _Tp = Tp();
    const double _Rs = Rs();
    const double _fc = fc();
    const int _nrn = nrn();
    for (int k = 0; k < _nan1; ++k) { // 循环每个方位向采样点
        for (int pointn = 0; pointn < point_num; ++pointn) { // 循环每个目标点
            // 生成目标回波
            double Rt = R0 - _Vr * k / _prf; // 当前时刻的目标距离 (调整为 0 基索引)
            VectorXd time_diff_target = tnrn.array() - 2 * Rt / c; // 目标回波时间差
            VectorXcd phase_target = (I_complex * PI * _gama * time_diff_target.array().square()).exp() // LFM 相位
                                   * precise_expj_2pi_scalar(-2.0 * Rt / _lambda);   // 距离相位(高精度)
            Array<bool, -1, 1> wint = (time_diff_target.array() >= 0) && (time_diff_target.array() <= _Tp); // 目标时间窗
            VectorXcd reT = wint.cast<double>() * amp_target * phase_target.array(); // 目标回波信号
            VectorXcd reT1 = awgn(reT, awgn_snr); // 为目标回波添加高斯白噪声
            Unj_Sig.col(k) += reT1; // 累加到未受干扰的回波信号中

            // 生成干扰信号
            for (int jn = 0; jn < num_jam; ++jn) { // 循环每个转发次数
                double Rj;
                if (jn < 3) { // 前三次转发
                    Rj = R0 - _Vr * k / _prf + (jn + 1) * detaR; // 计算第 jn 次转发的距离
                } else { // 后续转发
                    Rj = R0 - _Vr * k / _prf - (jn - 2) * detaR; // 计算第 jn 次转发的距离
                }
                VectorXd time_diff_jammer = tnrn.array() - 2 * Rj / c; // 干扰信号时间差
                VectorXcd phase_jammer = (I_complex * PI * _gama * time_diff_jammer.array().square()).exp() // LFM 相位
                                       * precise_expj_2pi_scalar(-2.0 * Rj / _lambda); // 距离相位(高精度,基于载波频率)
                Array<bool, -1, 1> winj = (time_diff_jammer.array() >= 0) && (time_diff_jammer.array() <= _Tp); // 干扰时间窗
                VectorXcd reJ = winj.cast<double>() * amp_jammer * phase_jammer.array(); // 干扰回波信号
        reJ = awgn(reJ, awgn_snr); // 为干扰信号添加高斯白噪声
                JMT_Sig.col(k) += reJ; // 累加到 MT 干扰信号中
            }
            echo_target.col(k) = JMT_Sig.col(k) + Radar_Sig.col(k); // 将干扰信号叠加到未受干扰信号上
        }
    }

    // JMT_Sig: MatrixXcd, 维度 nrn×nan1, 密集假目标干扰信号(复数,多次距离转发)
    saveMatrix("02_jamming_Case7_密集假目标干扰_DRFTJ.dat", JMT_Sig);
}



// 模式8:脉内前沿切片重复假目标干扰(Intra-pulse Leading-edge Slice Repeater Jamming)
inline void ganrao_Case8(const MatrixXcd& Radar_Sig, MatrixXcd& echo_target, MatrixXcd& IPLESRJ) {
    // 干扰参数设置
    double A_RJ = CFG.getDouble("jamming.case8_iplesrj.A_RJ", 30.0);
    double amp_target = CFG.getDouble("jamming.case8_iplesrj.amp_target", 1.0);
    double R0_new = CFG.getDouble("jamming.case8_iplesrj.R0_new", 608.0);
    double V_ISRJ = CFG.getDouble("jamming.case8_iplesrj.V_ISRJ", 0.0);
    int T_ISRJ_ratio = CFG.getInt("jamming.case8_iplesrj.T_ISRJ_ratio", 16);
    double T_ISRJ = Tp() / T_ISRJ_ratio;
    double R_ahead_ISRJ = CFG.getDouble("jamming.case8_iplesrj.R_ahead", 0.0);
    double awgn_snr = CFG.getDouble("jamming.case8_iplesrj.awgn_snr", 10.0);
    int N_ISRJ = static_cast<int>(Tp() / T_ISRJ);
    double amp_jammer = amp_target * pow(10, A_RJ / 20.0);

    // 计算目标初始参数
    double R0 = R0_new;  // 使用新的初始距离覆盖 module0.h 的 Rs
    double Kr = B() / Tp();  // 距离向线性调频率 (Hz/m)，与 Module0.h 的 gama 相同但重新定义以匹配 MATLAB

    // 调整距离向采样参数（与 Module0.h 参数不同）
    const int nrn_new = 2 * static_cast<int>(floor((fs() * Tp() + R0_new) / 2.0)); // 距离向采样点数 这里用R0_new 替代之前nrn中的wr 数值相同
    VectorXd tnrn_new =  (VectorXd::LinSpaced(nrn_new, 0, nrn_new - 1) / fs()).array() + R0_new/c  ;  // 距离向快时间向量

    // 使用字符串流拼接所有参数
    ostringstream oss;
    oss << "固定参数:\n"
        << "干扰机发射功率增益 (A_RJ): " << A_RJ << " dB\n"
        << "目标幅度 (amp_target): " << amp_target << "\n"
        << "雷达距目标初始距离 (R0_new): " << R0_new << " m\n"
        << "雷达与干扰机相对径向速度 (V_ISRJ): " << V_ISRJ << " m/s\n"
        << "干扰信号间隔时间 (T_ISRJ): " << T_ISRJ << " s\n"
        << "干扰机提前发射时间 (R_ahead_ISRJ): " << R_ahead_ISRJ << " s\n"
        << "干扰信号转发次数 (N_ISRJ): " << N_ISRJ << "\n"
        << "干扰幅度 (amp_jammer): " << amp_jammer << "\n"
        << "目标初始参数:\n"
        << "目标初始距离 (R0): " << R0 << " m\n"
        << "距离向线性调频率 (Kr): " << Kr << " Hz/m\n";
    cout << oss.str();      // 一次性打印所有参数


    IPLESRJ = MatrixXcd::Zero(nrn_new, nan1());      // 初始化 ISDJ 干扰信号矩阵
    echo_target = MatrixXcd::Zero(nrn_new, nan1());  // 初始化目标回波矩阵
    MatrixXcd Unj_Sig = MatrixXcd::Zero(nrn_new, nan1());  // 暂存目标信号

    // 主处理循环
    const int _nan1 = nan1();
    const double _Vr = Vr();
    const double _prf = prf();
    const double _Tp = Tp();
    const double _fc = fc();
    const double _B = B();
    const double _fs = fs();
    for (int k = 0; k < _nan1; ++k) { // 循环每个方位向采样点
        // 生成目标回波
        double R_t = R0 - _Vr * (k + 1) / _prf; // 实时目标距离 (调整为 0 基索引并与 MATLAB 逻辑一致)
        VectorXd time_diff_target = tnrn_new.array() - 2 * R_t / c; // 目标回波时间差
        VectorXcd phase_target = (I_complex * PI * Kr * time_diff_target.array().square()).exp() // LFM 相位
                               * precise_expj_2pi_scalar(-2.0 * _fc * R_t / c);      // 距离相位(高精度)
        Array<bool, -1, 1> win = (time_diff_target.array() < _Tp) && (time_diff_target.array() > 0); // 目标时间窗
        VectorXcd reT = win.cast<double>() * amp_target * phase_target.array(); // 目标回波信号
        reT = awgn(reT, awgn_snr);
        Unj_Sig.col(k) = reT;     // 暂存目标回波，后续叠加干扰

        // 生成干扰信号
        double R_ISRJ = R0 - V_ISRJ * (k + 1) / _prf - R_ahead_ISRJ; // 更新干扰机实时距离
        VectorXcd s_ISRJ2 = VectorXcd::Zero(nrn_new);     // 初始化单次循环的总干扰信号
        for (int Ni = 0; Ni < N_ISRJ; ++Ni) {         // 循环每个转发的干扰信号
            VectorXd time_diff_jammer = tnrn_new.array() - 2 * R_ISRJ / c - Ni * T_ISRJ; // 干扰信号时间差
            VectorXcd phase_jammer = (I_complex * PI * Kr * time_diff_jammer.array().square()).exp() // LFM 相位
                                   * precise_expj_2pi_scalar(-2.0 * _fc * R_ISRJ / c - _fc * Ni * T_ISRJ); // 距离相位(高精度)
            Array<bool, -1, 1> win1 = (time_diff_jammer.array() < T_ISRJ) && (time_diff_jammer.array() > 0); // 采样窗函数
            Array<bool, -1, 1> winjj = (time_diff_jammer.array() < _Tp) && (time_diff_jammer.array() > 0); // 防止溢出波门
            VectorXcd s_ISRJ1 = (win1 && winjj).cast<double>() * amp_jammer * phase_jammer.array(); // 单次转发干扰信号
            s_ISRJ1 = awgn(s_ISRJ1, awgn_snr); // 为干扰信号添加高斯白噪声
            s_ISRJ2 += s_ISRJ1; // 累加所有转发的干扰信号
        }
        IPLESRJ.col(k) = s_ISRJ2; // 存储各个脉冲的干扰信号到IPLESRJ
        echo_target.col(k) = IPLESRJ.col(k) + Radar_Sig.col(k); // 将干扰信号IPLESRJ叠加到Radar_Sig形成带干扰信号的回波信号echo_target
    }

    // IPLESRJ: MatrixXcd, 维度 nrn_new×nan1, 脉内前沿切片重复干扰信号(复数,独立采样参数体系)
    saveMatrix("02_jamming_Case8_脉内前沿切片干扰_IPLESRJ.dat", IPLESRJ);
}




// 模式9:频谱弥散干扰(SMSP: Spectral-Matched Spread Spectrum Jamming) 
inline void ganrao_Case9(const MatrixXcd& Radar_Sig, MatrixXcd& echo_target,MatrixXcd& JSMSP_Sig) 
{
    int num_slices = CFG.getInt("jamming.case9_smsp.num_slices", 4);
    double JSR = CFG.getDouble("jamming.case9_smsp.JSR", 15.0);
    double amp_target = CFG.getDouble("jamming.case9_smsp.amp_target", 1.0);
    double amp_extra = CFG.getDouble("jamming.case9_smsp.amp_extra", 1.4);
    double awgn_snr = CFG.getDouble("jamming.case9_smsp.awgn_snr", 10.0);
    double amp_jammer = amp_target * pow(10, JSR / 20) * amp_extra;
    double gama_j = num_slices * gama();
    double Tp_j = Tp() / num_slices;
    double R0 = CFG.getDouble("jamming.case9_smsp.R0", 10000.0);

    ostringstream oss;
    oss << "固定参数:\n"
        << "目标初始距离 (R0): " << R0 << " m\n"
        << "频谱切片次数 (num_slices): " << num_slices << "\n"
        << "干信比 (JSR): " << JSR << " dB\n"
        << "目标幅度 (amp_target): " << amp_target << "\n"
        << "干扰幅度 (amp_jammer): " << amp_jammer << "\n"
        << "干扰调频率 (gama_j): " << gama_j << " Hz/s\n"
        << "切片时宽 (Tp_j): " << Tp_j << " s\n";
    cout << oss.str();       // 一次性打印所有参数

    JSMSP_Sig = MatrixXcd::Zero(nrn(), nan1());     // 初始化干扰信号
    MatrixXcd Unj_Sig = MatrixXcd::Zero(nrn(), nan1());  // 初始化原始回波信号
    echo_target = MatrixXcd::Zero(nrn(), nan1());      // 初始化混叠回波信号

    // 主处理循环
    const int _nan1 = nan1();
    const double _Vr = Vr();
    const double _prf = prf();
    const double _gama = gama();
    const double _lambda = lambda();
    const double _Tp = Tp();
    const double _Rs = Rs();
    const double _fc = fc();
    const double _B = B();
    const int _nrn = nrn();
    for (int k = 0; k < _nan1; ++k) {

        double Rt = R0 - _Vr * k / _prf;   // 目标距离随时间变化
        VectorXd time_diff_target = tnrn.array() - 2*Rt/c;  // 计算目标距离与回波信号的时间差
        // LFM调频相位(数值小,直接计算) + 距离相位(数值大,高精度计算)
        VectorXcd chirp_phase = (I_complex * PI * _gama * time_diff_target.array().square()).exp();
        complex<double> dist_phase = precise_expj_2pi_scalar(-2.0 * Rt / _lambda);
        Array<bool, -1, 1> wint = (time_diff_target.array() >= 0) && (time_diff_target.array() <= _Tp);      // 时域截断窗
        VectorXcd reT = wint.cast<double>()
                        * amp_target
                        * chirp_phase.array() * dist_phase;

        reT = awgn(reT, awgn_snr);
        Unj_Sig.col(k) = reT;    // 累加当前脉冲的干扰信号到Unj_Sig

        double Rj = Rt;       // 干扰机与目标同位置
        VectorXcd s_ISRJ = VectorXcd::Zero(_nrn);    // 初始化切片干扰信号

        for (int jn = 0; jn < num_slices; ++jn) {
            const double t_offset = jn * Tp_j;   // 切片时间偏移
            VectorXd time_diff_jammer = tnrn.array() - 2*Rj/c - t_offset;   // 干扰时间差
                        
            // 增强调频率的相位计算: LFM相位(小值) + 距离相位(大值,高精度)
            VectorXcd chirp_phase_j = (I_complex * PI * gama_j * time_diff_jammer.array().square()).exp();
            complex<double> dist_phase_j = precise_expj_2pi_scalar(-2.0 * (Rj + c*t_offset/2) / _lambda);
            Array<bool, -1, 1> winj = (time_diff_jammer.array() >= 0) && (time_diff_jammer.array() <= Tp_j);
            VectorXcd slice = winj.cast<double>()
                            * amp_jammer
                            * chirp_phase_j.array() * dist_phase_j;

            slice = awgn(slice, awgn_snr);   // 给切片干扰添加高斯白噪声
            s_ISRJ += slice;
        }

        JSMSP_Sig.col(k) = s_ISRJ;       // 累加切片干扰信号到JSMSP_Sig
        echo_target.col(k) = Radar_Sig.col(k) + JSMSP_Sig.col(k);    // 叠加切片干扰信号到Radar_Sig形成含切片干扰回波echo_target
    }

    // JSMSP_Sig: MatrixXcd, 维度 nrn×nan1, 频谱弥散干扰信号(复数,增强调频率切片)
    saveMatrix("02_jamming_Case9_频谱弥散干扰_SMSP.dat", JSMSP_Sig);
}





// 模式10:梳状谱干扰(Comb Spectrum Jamming)
inline void ganrao_Case10(const MatrixXcd& Radar_Sig, MatrixXcd& echo_target,MatrixXcd& JCOMB_Sig) 
{
    // 干扰参数
    int    num_tones = CFG.getInt("jamming.case10_comb.num_tones", 7);
    double JSR = CFG.getDouble("jamming.case10_comb.JSR", 0.0);
    double amp_target = CFG.getDouble("jamming.case10_comb.amp_target", 1.0);
    double deltaf = CFG.getDouble("jamming.case10_comb.deltaf", 1e6);
    double awgn_snr = CFG.getDouble("jamming.case10_comb.awgn_snr", 10.0);
    int    mid_tones = num_tones / 2;
    double R0 = CFG.getDouble("jamming.case10_comb.R0", 10000.0);
    double amp_jammer = amp_target * pow(10, JSR / 20);

    ostringstream oss;
    oss << "固定参数:\n"
        << "目标初始距离 (R0): " << R0 << " m\n"
        << "频谱线数量 (num_tones): " << num_tones << "\n"
        << "干信比 (JSR): " << JSR << " dB\n"
        << "目标幅度 (amp_target): " << amp_target << "\n"
        << "频率间隔 (deltaf): " << deltaf << " Hz\n"
        << "中心频点索引 (mid_tones): " << mid_tones << "\n"
        << "干扰幅度 (amp_jammer): " << amp_jammer << "\n";
    cout << oss.str();         // 一次性打印所有参数

    JCOMB_Sig = MatrixXcd::Zero(nrn(), nan1());     // 初始化干扰信号
    echo_target = MatrixXcd::Zero(nrn(), nan1());   // 初始化混叠回波信号
    MatrixXcd Unj_Sig = MatrixXcd::Zero(nrn(), nan1());  // 初始化原始回波信号

    // 遍历每个脉冲
    const int _nan1 = nan1();
    const double _Vr = Vr();
    const double _prf = prf();
    const double _gama = gama();
    const double _lambda = lambda();
    const double _Tp = Tp();
    const double _Rs = Rs();
    const double _fc = fc();
    const double _B = B();
    const int _nrn = nrn();
    for (int k = 0; k < _nan1; ++k) {
        double Rt = R0 - _Vr * k / _prf;     //  目标距离随时间变化

        VectorXd time_diff_target = tnrn.array() - 2*Rt/c;
        
        // LFM调频相位(数值小,直接计算) + 距离相位(数值大,高精度计算)
        VectorXcd chirp_phase = (I_complex * PI * _gama * time_diff_target.array().square()).exp();
        complex<double> dist_phase = precise_expj_2pi_scalar(-2.0 * Rt / _lambda);
        Array<bool, -1, 1> wint = (time_diff_target.array() >= 0) && (time_diff_target.array() <= _Tp);
        VectorXcd reT = wint.cast<double>()
                        * chirp_phase.array() * dist_phase
                        * amp_target; 

        reT = awgn(reT, awgn_snr);
        Unj_Sig.col(k) = reT;     // 将原始回波信号保存到Unj_Sig

        double Rj = Rt;     // 干扰机与目标同位置
        VectorXcd s_comb = VectorXcd::Zero(_nrn);   // 初始化 comb 切片干扰信号

        for (int jn = 0; jn < num_tones; ++jn) {
            // 频率偏移计算
            double fi = (jn <= mid_tones) ? ( (jn+1) * deltaf ) :  (-(jn - mid_tones + 1) * deltaf) ;   // 根据中心频点计算频率偏移

            // 生成干扰窗
            VectorXd time_diff_jammer = tnrn.array() - 2*Rj/c;   // 干扰时间差
            
            // 相位计算: LFM相位(小值) + 频偏相位(小值) + 距离相位(大值,高精度)
            VectorXcd chirp_freq_phase = (I_complex * PI * _gama * time_diff_jammer.array().square()
                                         - I_complex * 2.0 * PI * fi * time_diff_jammer.array()).exp();
            complex<double> dist_phase_j = precise_expj_2pi_scalar(-2.0 * Rj / _lambda);
            Array<bool, -1, 1> winj = (time_diff_jammer.array() >= 0) && (time_diff_jammer.array() <= _Tp);
            VectorXcd slice = winj.cast<double>()
                            * amp_jammer
                            * chirp_freq_phase.array() * dist_phase_j;    
            slice = awgn(slice, awgn_snr);             // 给 comb 切片干扰添加高斯白噪声
            s_comb += slice;
        }

        JCOMB_Sig.col(k) = s_comb;      // 累加各个脉冲的comb切片干扰信号到JCOMB_Sig
        echo_target.col(k) = Radar_Sig.col(k) + JCOMB_Sig.col(k);   // 叠加 comb 切片干扰信号到Radar_Sig形成含 comb 切片干扰回波echo_target
    }

    // JCOMB_Sig: MatrixXcd, 维度 nrn×nan1, 梳状谱干扰信号(复数,多频点线性叠加)
    saveMatrix("02_jamming_Case10_梳状谱干扰_COMB.dat", JCOMB_Sig);
}



/********************************************** 干扰生成函数 结束 ************************************************/

/*
*  模式 1:距离假目标干扰(RDJ: Range Deception Jamming)
*    过程:
*      1) 初始化 RDJ_Sig(nrn×nan1) 和 echo_target(nrn×nan1)
*      2) 设定干扰参数:延迟脉冲数 jj=1，假目标距离 Rj=100m，时延 t0=2Rj/c
*      3) 前 jj 个脉冲保持原始信号
*      4) 从第 jj+1 个脉冲开始:
*         a) 对前向脉冲信号做 FFT 转换到频域
*         b) 施加时延相位 exp(-j2πfΔt0)
*         c) IFFT 转换回时域生成干扰信号
*         d) 将干扰信号按 amp_j 增益叠加到原始信号
*
*    原理:
*      通过频域相位调制模拟不同距离目标的回波特性，利用雷达脉冲压缩处理中时延与距离的线性关系，
*      在距离维度生成虚假目标点迹，破坏雷达对真实目标的距离分辨能力.
*
*    注意事项:
*      - 假目标距离 Rj 应大于雷达距离分辨率(ΔR=c/(2B))且小于最大不模糊距离(Rmax=c/(2PRF))
*      - 干扰延迟脉冲数 jj 需根据雷达扫描模式设置，典型值为1-3个脉冲周期
*      - FFT/IFFT 需保持信号长度与采样率匹配，建议使用2^N点数并添加窗函数减少频谱泄漏
*      - 干扰幅度 amp_j 建议比真实目标高3-6dB以确保假目标被优先检测

*  模式 2:速度假目标干扰(VDJ: Velocity Deception Jamming)
*    过程:
*      1) 初始化 VDJ_Sig(nrn×nan1) 和 echo_target(nrn×nan1)
*      2) 设定参数:假目标速度 Vj=100km/s，延迟脉冲数 jj=1
*      3) 计算真实目标多普勒频率 fd=2Vr/λ，假目标距离 Rj=Vj*(k-jj)/PRF
*      4) 前 jj 个脉冲保持原始信号
*      5) 从第 jj+1 个脉冲开始:
*         a) 计算当前脉冲假目标多普勒频率 fj=2Vj/λ
*         b) 施加多普勒调制 exp(j2πfj*t)
*         c) FFT转换到频域后叠加时延相位 exp(-j2πfΔt0)
*         d) IFFT生成干扰信号并叠加
*
*    原理:
*      利用多普勒频移与速度的线性关系(fd=2v/λ)，通过动态调整调制频率，在雷达多普勒滤波器组中
*      形成虚假速度通道响应，干扰雷达的速度跟踪环路.
*
*    注意事项:
*      - 假目标速度 Vj 应超出雷达速度盲区(|Vj| > λ*PRF/4)但小于最大不模糊速度
*      - 多普勒调制需考虑雷达相干处理间隔(CPI)，确保假目标在多普勒维有效积累
*      - 时延相位项需与速度调制同步更新，模拟运动目标的距离-多普勒耦合效应
*      - 建议采用速度渐变策略防止雷达CFAR检测到异常速度跳变

*  模式 3:间歇采样转发干扰(ISRJ: Interrupted Sampling Repeater Jamming)
*    过程:
*      1) 初始化 s_ISRJ(nrn×nan1)，设定采样周期 Ts=4μs，采样脉宽 T_ISRJ=1μs
*      2) 计算采样点数 Ns=round(Ts*fs)，脉宽点数 Nw=round(T_ISRJ*fs)
*      3) 设定假目标距离 Rj，计算时延 t0=2Rj/c
*      4) 对每个脉冲:
*         a) 检测信号有效区域(超过噪声门限的连续区间)
*         b) 生成间歇采样窗口(周期Ns，脉宽Nw)
*         c) 截取采样段信号并进行K次多普勒切片重组
*         d) 每次重组施加频移Δf=k*PRF/K，叠加时延相位后累加
*         e) 加权干扰信号并与原始信号叠加
*
*    原理:
*      通过周期性采样和参数化重组，在距离-多普勒二维平面产生多个虚假目标，形成"弹幕"式干扰效果.
*      时域采样导致频谱卷积，频域切片重组产生多普勒偏移，形成距离-速度联合欺骗.
*
*    注意事项:
*      - 采样周期需满足 Ts > 1/B 以确保距离可分性
*      - 切片次数 K 建议设为雷达多普勒通道数的整数倍以增强干扰密度
*      - 重组时应保持信号相位连续性，避免引入虚假相位跃变
*      - 建议采用随机切片间隔策略防止雷达通过重频参差识别干扰模式

*  模式 4:窄带噪声干扰(Narrowband Noise Jamming)
*    过程:
*      1) 复用时间向量 t(nrn×1)，设定噪声信号长度 L=nrn
*      2) 初始化 fft_lvbo_z1(nrn×nan1)存储滤波后频域信号
*      3) 对每个脉冲:
*         a) 生成复高斯白噪声 J=randn(L,1)+1j*randn(L,1)
*         b) FFT转换到频域 J_fft=fft(J)
*         c) 设计8阶低通滤波器，截止频率 fc=0.2*fs
*         d) 滤波处理:J_filt=J_fft.*H_filter
*         e) 抑制高频分量:J_filt(f>fc)=min(abs(J_filt))
*         f) IFFT转换回时域得到窄带干扰信号
*         g) 叠加到原始信号形成 echo_target
*
*    原理:
*      通过带限噪声在雷达工作带宽内产生遮盖式干扰，降低接收机信噪比，使雷达无法有效检测真实目标.
*      窄带特性确保干扰能量集中，提高干扰功率利用率.
*
*    注意事项:
*      - 噪声功率应满足 Pj > P_signal + P_noise + 处理增益
*      - 滤波器过渡带需陡峭以抑制带外噪声，建议使用等波纹FIR滤波器
*      - 对于LFM雷达，建议噪声带宽覆盖信号瞬时带宽的50%-80%
*      - 可结合频率瞄准技术，动态调整噪声中心频率跟踪雷达工作频率

*  模式 5:距离门拖引干扰(JRGPO: Range Gate Pull-Off Jamming)
*    过程:
*      1) 初始化 JRGPO_Sig(nrn×nan1) 和 echo_target(nrn×nan1)
*      2) 设定拖引参数:速度 Vj=340m/s，幅度增益 amp_RGPO=1.4(3dB)
*      3) 分三阶段处理:
*         a) 捕获阶段(k=0): Rj=Rt - Vj*Δt 建立初始偏移
*         b) 拖引阶段(1≤k<4): Rj=Rt - Vj*k*Δt 匀速拖离
*         c) 反转阶段(k≥4): Rj=Rt + Vj*(k-3)*Δt 破坏跟踪
*      4) 对每个脉冲:
*         a) 计算真实目标距离 Rt=R0 - Vr*k*Δt
*         b) 生成时域矩形窗 wint/winj 截取信号
*         c) 计算调频相位 exp(jπγ(t-2R/c)^2)
*         d) 叠加距离相位 exp(±j4πR/λ)
*         e) 添加10dB高斯白噪声后信号合成
*
*    原理:
*      通过渐进改变假目标距离，诱使雷达距离跟踪波门偏离真实目标.捕获阶段建立假目标优势，
*      拖引阶段逐步扩大偏差，反转阶段破坏跟踪收敛性，最终导致雷达失锁.
*
*    注意事项:
*      - 拖引速度应满足 Vj > 雷达跟踪环带宽对应的速度门限
*      - 相位符号需严格匹配:真实目标用-4πR/λ，干扰目标用+4πR/λ
*      - 时间窗需精确控制为[-Tp/2,Tp/2]以避免信号畸变
*      - 建议拖引持续时间设置为雷达跟踪环时间常数的3-5倍
*      - 终止距离应超出雷达距离波门最大跟踪范围

*  模式 6:速度门拖引干扰(JRGPO: Velocity Gate Pull-Off Jamming)
*    过程:
*      1) 初始化 JVGPO_Sig(nrn×num_jam) 和 echo_target(nrn×num_jam)，设定目标点数 point_num=1
*      2) 设定雷达参数，包括载波频率 fc, 脉宽 Tp, 带宽 B 等
*      3) 计算真实目标距离 R0 和雷达与目标的初始位置信息 [x0,y0,z0]
*      4) 对于每次拖引(k=1:num_jam):
*         a) 计算当前脉冲的真实目标距离 Rt=R0 + Vr * k / prf
*         b) 根据时间向量 tnrn 生成窗口函数 wint/winj
*         c) 生成目标回波信号 reT，并施加调频相位和多普勒效应
*         d) 若为第一次拖引(k<=1)，计算假目标速度对应的频率偏移 fj 并生成干扰回波信号 reJ
*            施加调频相位、多普勒效应及额外的速度调整以实现与真实目标的分离
*         e) 对于第2至第4次拖引(k<=4)，重复步骤d)，但使用不同的速度偏移策略
*         f) 对于第5次及以后的拖引(k>4)，反转速度偏移方向，逐渐破坏雷达跟踪性能
*         g) 将目标回波信号 Radar_Sig 和干扰回波信号 JVGPO_Sig 分别累加并合成到 echo_target
*
*    原理:
*      利用速度维度上的渐进变化来误导雷达的速度跟踪系统.通过在速度维制造虚假的目标运动，
*      诱使雷达速度跟踪门偏离真实值，从而降低雷达对真实目标的速度分辨能力.
*
*    注意事项:
*      - 拖引速度 Vj 需超过雷达速度分辨率以确保有效欺骗
*      - 多普勒调制需精确控制以模拟真实的移动目标特性
*      - 在进行速度拖引时，应考虑雷达相干处理间隔(CPI)的影响，确保干扰效果在整个CPI内保持一致
*      - 干扰信号的幅度增益 amp_VGPO 应适当设置，过高可能导致雷达检测异常而被识别为干扰
*      - 反转阶段的速度调整策略需要根据具体雷达系统的跟踪灵敏度进行优化，避免过早暴露干扰行为
*      - 建议结合其他干扰模式(如RDJ或ISAR)以增强整体干扰效果，提高对抗复杂雷达系统的成功率

*  模式7:密集复制转发假目标干扰(Dense Repeater False Target Jamming)
*    过程:
*      1) 初始化 JMT_Sig(nrn×nan1) 和 echo_target(nrn×nan1)，设定目标点数 point_num=1
*      2) 配置干扰参数，包括转发次数 num_jam=50，距离增量 detaR=50m，干扰增益 JSR=0dB
*      3) 计算目标初始位置 [x0,y0,z0] 和初始斜距 R0
*      4) 对于每个方位向脉冲(k=0:nan1-1):
*         a) 计算真实目标动态距离 Rt=R0 - Vr*k/prf
*         b) 生成真实回波信号 reT:
*            - 计算时间延迟 t_delay_target = tnrn - 2Rt/c
*            - 生成时域窗 wint(0≤t≤Tp)
*            - 施加调频相位 exp(jπγt?) 和距离相位 exp(-j4πR/λ)
*         c) 对于每次转发(jn=0:num_jam-1):
*            i) 计算干扰距离 Rj=Rt±jn*detaR(前3次正向偏移，后续反向偏移)
*            ii) 生成干扰时延 t_delay_jam = tnrn - 2Rj/c 
*            iii) 构建干扰窗 winj(0≤t≤Tp)
*            iv) 生成调频相位 exp(jπγt?) 和载波相位 exp(-j2πfc*2Rj/c)
*            v) 合成干扰信号 reJ 并累加到当前脉冲
*         d) 对真实回波和干扰信号进行AWGN加噪处理
*         e) 将合成信号存入 echo_target
*
*    原理:
*      通过距离维的密集假目标生成实现欺骗干扰。在真实目标回波附近产生多个等间隔假目标，
*      利用雷达距离分辨率的局限性，形成具有相同运动特性的虚假目标群，破坏雷达的目标检测
*      与跟踪能力。前向偏移构建虚假接近目标，反向偏移模拟远离目标，形成动态欺骗效果。
*
*    注意事项:
*      - 转发次数 num_jam 应设置为奇数以保证对称干扰模式
*      - 距离增量 detaR 需大于雷达距离分辨率 ρr=c/(2B)=3.75m
*      - 干扰幅度 amp_MT 应保持与真实目标相当(JSR=0dB)
*      - 载波相位项需精确计算 2πfc*(2R/c) 保证相干性
*      - 时域窗判断应严格匹配脉冲持续时间[0,Tp]
*      - 建议结合速度拖引干扰形成复合干扰样式
*      - 注意控制噪声功率，避免过高的SNR影响干扰隐蔽性

*  模式8:脉内前沿切片重复假目标干扰(Intra-pulse Leading-edge Slice Repeater Jamming)
*    过程:
*      1) 初始化 IPLESRJ(nrn×nan1) 和 echo_target(nrn×nan1)
*      2) 配置干扰参数：切片间隔 T_ISRJ=Tp/16，转发次数 N_ISRJ=16，功率增益 A_RJ=30dB
*      3) 计算专用初始距离 R0_mode8=608m 避免与全局参数冲突
*      4) 对于每个方位向脉冲(m=0:nan1-1):
*         a) 计算干扰机动态距离 R_ISRJ = R0 - V_ISRJ*m/prf - R_ahead_ISRJ
*         b) 初始化当前脉冲干扰信号 s_ISRJ2
*         c) 对于每次切片转发(Ni=1:N_ISRJ):
*            i) 计算复合时延 t_delay = tnrn - 2R_ISRJ/c - Ni*T_ISRJ
*            ii) 生成双条件时域窗(win1:切片窗, winjj:脉宽保护窗)
*            iii) 构建调频相位 exp(jπKr*t?) 和载波相位 exp(j2πfc(-2R/c-Ni*T))
*            iv) 合成切片干扰信号并加噪
*            v) 累加到当前脉冲干扰
*         d) 将切片干扰与原始回波合成
*    原理:
*      通过对接收信号的前沿切片进行多次重复转发，在时频域形成密集假目标群。利用雷达信号处理
*      的匹配滤波特性，在距离维产生等间隔假目标，通过时延叠加形成多个虚假峰值，破坏雷达对真实
*      目标的检测与跟踪。
*    注意事项:
*      - 切片间隔 T_ISRJ 应小于雷达距离分辨单元时间(c/(2B)=3.75ns)
*      - 双时域窗设计确保切片在脉宽范围内且不溢出
*      - 载波相位需包含时延项 Ni*T_ISRJ 保证相干性
*      - 专用距离参数 R0_mode8 避免与全局场景参数冲突
*      - 建议结合DRFM技术实现精确信号存储与重构
*      - 注意控制切片数量防止距离像过度展宽

*  模式9:频谱弥散干扰(Spectral-Matched Spread Spectrum Jamming)
*    过程:
*      1) 初始化 JSMSP_Sig(nrn×nan1) 和 echo_target(nrn×nan1)
*      2) 配置干扰参数：子脉冲数 num_jam=4，调频率增益 gama_j=4*gama
*      3) 计算目标初始位置及动态距离 Rt
*      4) 对于每个方位向脉冲(k=0:nan1-1):
*         a) 生成真实回波信号 reT：
*            - 时域窗 wint∈[0,Tp]
*            - 调频相位 exp(jπγt?)
*            - 距离相位 exp(-j4πR/λ)
*         b) 生成SMSP干扰：
*            i) 对每个子脉冲(jn=0:num_jam-1):
*               - 计算时延 t_delay = tnrn - 2Rj/c - jn*Tp_j
*               - 生成子脉冲窗 winj∈[0,Tp_j]
*               - 构建增强调频相位 exp(jπγ_jt?)
*               - 载波相位 exp(-j2πfc(2R/c + jn*Tp_j))
*               - 合成子脉冲并累加
*         c) 将真实回波与干扰信号合成
*    原理:
*      通过将原始信号分割为多个调频率增强的子脉冲，在频域产生频谱弥散效应。
*      利用匹配滤波处理的失配特性，在距离像主瓣两侧生成虚假峰，降低雷达的
*      检测信噪比和测距精度。
*    注意事项:
*      - 子脉冲时宽 Tp_j 必须严格等于 Tp/num_jam
*      - 调频率增强倍数需与子脉冲数匹配(gama_j=num_jam*gama)
*      - 载波相位需补偿时延项 jn*Tp_j 保证相干性
*      - 建议子脉冲数不超过8以免过度降低干扰功率密度
*      - 需保持子脉冲间的严格时间同步
*      - 适用于对抗线性调频体制雷达，对相位编码信号效果有限

*  模式10:梳状谱干扰(Comb Spectrum Jamming)
*    过程:
*      1) 初始化 JCOMB_Sig(nrn×nan1) 和 echo_target(nrn×nan1)
*      2) 配置干扰参数：频率步长 detaf=1MHz，转发次数 num_jam=7
*      3) 计算目标初始位置及动态距离 Rt
*      4) 对于每个方位向脉冲(k=0:nan1-1):
*         a) 生成真实回波信号 reT：
*            - 时域窗 wint∈[0,Tp]
*            - 调频相位 exp(jπγt?)
*            - 距离相位 exp(-j4πR/λ)
*         b) 生成梳状谱干扰：
*            i) 对每个子脉冲(jn=0:num_jam-1):
*               - 计算正负频率偏移 fi=±jn*detaf
*               - 生成干扰时域窗 winj∈[0,Tp]
*               - 构建调频相位 exp(jπγt?)
*               - 载波相位 exp(-j2πfc(2R/c))
*               - 频移相位 exp(-j2πfi*t)
*               - 合成子脉冲并累加
*         c) 合成总回波信号
*    原理:
*      通过在多个离散频率点生成干扰信号，在雷达接收机匹配滤波后形成等间隔的假目标群。
*      利用频谱的梳状结构特性，导致雷达在多个距离门检测到虚假目标，破坏目标检测和跟踪。
*    注意事项:
*      - 频率步长 detaf 需大于雷达距离分辨率Δf=1/Tp≈83kHz
*      - 正负频率偏移需对称分布以形成规则梳状谱
*      - 载波相位需精确补偿传播时延
*      - 建议转发次数为奇数以保证中心频率无干扰
*      - 需控制干扰功率防止主瓣掩盖真实目标
*      - 适用于对抗脉冲压缩雷达，对MTI雷达效果有限

*    通用注意事项:
*      - 所有干扰模式均可通过设置随机种子(srand(seed))实现可重复性测试
*      - 实际部署时应根据雷达参数动态调整干扰参数:
*          * 距离相关参数需考虑c/(2B)分辨率限制
*          * 速度相关参数受λ*PRF/4不模糊速度约束
*          * 干扰幅度建议通过实时DRFM采样动态校准
*      - 多模式复合干扰建议采用时分复用或空分复用策略
*/

#endif // MODULE2_H

