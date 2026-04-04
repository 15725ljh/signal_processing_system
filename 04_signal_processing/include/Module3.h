// 本模块包含了所有模式的信号处理函数内容(Case1~6)
#ifndef MODULE3_H
#define MODULE3_H
#include "Module0.h"          // 工具函数(FFT/IFFT/saveMatrix/JamLocated等) + 全局参数
#include "JamTarDivi.h"       // 干扰解耦(Tsallis交叉熵时频分割, common/目录统一实现)
#include "Config.h"           // 配置管理器(用于读取processing.*参数)
#define CFG Config::instance()  // 配置管理器单例快捷宏

int chuli();                  // 信号处理主函数:自动遍历Case1~6,执行脉冲压缩和多普勒处理

// ── 高精度辅助函数 ──
// 当 freq*t 数值很大时,直接计算 exp(j*2*PI*x) 会因双精度丢失小数部分而精度不足.
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

/********************************************** 信号处理函数 开始 ************************************************/

// 模式1:跳频信号处理(带速度补偿与解模糊)
inline void chuli_Case1(const MatrixXcd& Radar_Sig,MatrixXcd& F) {

    // 动态下变频处理(高精度:避免 freq*tnrn 精度损失)
    // 计算下变频因子:exp(-j*2*pi*f(j)*tnrn(i)), 逐列用数字载波方法精确计算
    const int nrn_c1 = nrn(), nan1_c1 = nan1();
    const double fs_c1 = fs(), Tstart_c1 = Tstart();
    MatrixXcd downConversion(nrn_c1, nan1_c1);
    for (int j = 0; j < nan1_c1; ++j) {
        downConversion.col(j) = precise_expj_2pi_array(-f(j), tnrn.array(), fs_c1, Tstart_c1);
    }
    // 动态下变频:对 Radar_Sig 的每个元素进行乘法
    MatrixXcd s_hui = Radar_Sig.array() * downConversion.array();

    // 频域滤波处理
    // 对距离向信号进行 FFT 中心化处理:fftshift(fft(fftshift(s_hui,1),[],1),1)
    MatrixXcd S_hui(s_hui.rows(), s_hui.cols());
    for (int j = 0; j < s_hui.cols(); j++) {
        VectorXcd col = s_hui.col(j);  // 提取第 j 列数据
        col = fftshift(col);   // 先进行 fftshift
        col = fft(col);        // 调用 Module0.h 中提供的 fft 函数
        col = fftshift(col);   // 再次 fftshift
        S_hui.col(j) = col;
    }
    // 频域滤波:S = win .* S_hui, 其中 win 为 nrn()×1,对每一列逐元素相乘
    MatrixXcd S = S_hui;
    for (int j = 0; j < S.cols(); j++) {
        S.col(j) = S.col(j).array() * win.array().cast<complex<double>>();
    }
    // 时域反变换(观察滤波效果):S_hui1 = fftshift(ifft(fftshift(S,1),[],1),1)
    MatrixXcd S_hui1(S.rows(), S.cols());
    for (int j = 0; j < S.cols(); j++) {
        VectorXcd col = S.col(j);
        col = fftshift(col);
        col = ifft(col);
        col = fftshift(col);
        S_hui1.col(j) = col;
    }

    // 脉冲压缩处理
    // 构造对称频率向量:f_range = ((-nrn()/2+1):(nrn()/2)).' / nrn() * fs();
    const int _nrn = nrn();
    const double _fs = fs();
    const double _gama = gama();
    VectorXd f_range(_nrn);
    for (int i = 0; i < _nrn; i++) {
        f_range(i) = ((i - (_nrn / 2) + 1.0) / _nrn) * _fs;
    }
    VectorXcd Hr(_nrn);
    for (int i = 0; i < _nrn; i++) {
        Hr(i) = exp(I_complex * PI * (f_range(i) * f_range(i)) / _gama);
    }
    // 频域脉冲压缩:S2 = S .* Hr, Hr 为列向量,逐列相乘
    MatrixXcd S2 = S;
    for (int j = 0; j < S2.cols(); j++) {
        S2.col(j) = S2.col(j).array() * Hr.array();
    }
    // 时域反变换得到脉冲压缩结果:s_pc = fftshift(ifft(fftshift(S2,1),[],1),1)
    MatrixXcd s_pc(S2.rows(), S2.cols());
    for (int j = 0; j < S2.cols(); j++) {
        VectorXcd col = S2.col(j);
        col = fftshift(col);
        col = ifft(col);
        col = fftshift(col);
        s_pc.col(j) = col;
    }

    // 多普勒补偿
    // 计算多普勒补偿因子:exp(1j * 2*pi * fr * 2*Vr() * (pulse index) / (prf()*c))
    // 构造脉冲索引向量(1×nan1()),元素为 1,2,...,nan1()
    RowVectorXd pulseIndices = RowVectorXd::LinSpaced(nan1(), 1, nan1());
    // 构造多普勒相位补偿矩阵,尺寸:nrn()×n_pulses
    const int _nan1 = nan1();
    const double _prf = prf();
    const double _Vr = Vr();
    MatrixXcd dopplerCompensation(nrn(), nan1());
    for (int i = 0; i < _nrn; i++) {
        for (int j = 0; j < _nan1; j++) {
            double phase = -2.0 * PI * fr(i) * (2 * _Vr * pulseIndices(j)) / (_prf * c);
            dopplerCompensation(i, j) = exp(I_complex * phase);
        }
    }
    // 应用多普勒补偿:s_bu = S2 .* dopplerCompensation
    MatrixXcd s_bu = S2.array() * dopplerCompensation.array();
    // 转换到时域:s_buT = fftshift(ifft(fftshift(s_bu,1),[],1),1)
    MatrixXcd s_buT(s_bu.rows(), s_bu.cols());
    for (int j = 0; j < s_bu.cols(); j++) {
        VectorXcd col = s_bu.col(j);
        col = fftshift(col);
        col = ifft(col);
        col = fftshift(col);
        s_buT.col(j) = col;
    }

    // 距离-多普勒耦合补偿
    
    VectorXd xi = tnrn * (c / 2.0);   // 将时间转换为距离:xi = tnrn * c/2
    VectorXd freq = f;  // 使用跳频序列 f 作为 freq

    MatrixXcd lx6(nrn(), nan1());  // 对每个脉冲 m 进行补偿
    for (int m = 0; m < _nan1; m++) {
        // 对于每个距离采样点,计算补偿因子 exp(1j*2*pi*2*xi/c*freq(m))
        VectorXcd compensation(_nrn);
        for (int i = 0; i < _nrn; i++) {
            double phase = 2.0 * PI * (2 * xi(i) / c) * freq(m);
            compensation(i) = exp(I_complex * phase);
        }
        lx6.col(m) = s_buT.col(m).array() * compensation.array();
    }

    // 速度解模糊处理
    double v_max = c / (2.0 * prt() * fc());   // 计算最大不模糊速度 v_max = c/(2*prt()*fc())
    // 计算模糊次数 NUM = floor((Vr() + v_max/2) / v_max) (Vr>0 = approaching)
    int NUM = static_cast<int>(floor((Vr() + v_max / 2.0) / v_max));
    // 计算速度轴 dv
    // dv = (((0:nan1()-1) - nan1()/2) * c/(2*prt()*fc()*nan1())) + NUM*(c/(2*prt()*fc()));
    const double _prt = prt();
    const double _fc = fc();
    VectorXd dv(nan1());
    for (int j = 0; j < _nan1; j++) {
        dv(j) = ((j - _nan1 / 2.0) * c / (2.0 * _prt * _fc * _nan1)) + NUM * (c / (2.0 * _prt * _fc));
    }

    // 多普勒聚焦处理
    // 初始化距离-多普勒矩阵 F,维度:nrn() × nan1()
    // MatrixXcd F = MatrixXcd::Zero(nrn(), nan1());
    // 对每个距离门 g 进行处理
    for (int g = 0; g < _nrn; g++) {
        RowVectorXcd Temp = lx6.row(g);      // 提取当前距离门数据 Temp(1×nan1())
        for (int k = 0; k < _nan1; k++) {     // 对每个多普勒单元 k(0 至 nan1()-1)计算相干积累
            RowVectorXcd W(_nan1);            // 构建相位补偿向量 W,尺寸:1×nan1()      
            double factor = (static_cast<double>(k) / _nan1 - 0.5 + NUM);
            for (int m = 0; m < _nan1; m++) {
                double phase = -2.0 * PI * (freq(m) / _fc) * factor * m;
                W(m) = exp(I_complex * phase);
            }
            // 计算 F(g,k) = sum(W .* Temp)
            complex<double> sumVal = 0.0;
            for (int m = 0; m < _nan1; m++) {
                sumVal += W(m) * Temp(m);
            }
            F(g, k) = sumVal;
        }
    }

    // F: MatrixXcd, 维度 nrn×nan1, 距离-多普勒图(复数,含跳频速度补偿与解模糊)
    saveMatrix("04_processing_Case1_距离多普勒图_rdmap.dat", F);

}
    




// 模式2:固定载频信号处理(含随机相位补偿)
inline void chuli_Case2(const MatrixXcd& Radar_Sig,MatrixXcd& F) {
    
    // 下变频处理:消除固定载频调制
    // 计算 exp(-1i*2*pi*fc()*tnrn),tnrn为 nrn() x 1 向量
    // (高精度:使用数字载波替代直接计算 fc*tnrn 以避免精度损失)
    VectorXcd expVec = precise_expj_2pi_array(-fc(), tnrn.array(), fs(), Tstart());
    // 将 expVec 复制扩展为与 Radar_Sig 尺寸相同(nrn() x nan),用于矩阵点乘
    MatrixXcd s_hui = Radar_Sig.array() * expVec.replicate(1, nan1()).array();
    
    // 随机相位补偿:消除干扰端加上的随机相位调制
    // 注意:phi1 在MATLAB()中为1 x nan,故这里取转置后的共轭得到 1 x nan1() 行向量
    // 使用 replicate 将行向量扩展为 nrn() x nan1() 与 s_hui 对应
    MatrixXcd phi1_rep = phi1.conjugate().transpose().replicate(nrn(), 1);
    s_hui = s_hui.array() * phi1_rep.array();
    
    // 距离向处理
    // 进行距离向FFT:先对 s_hui 每列进行 fftshift,再对每列进行 FFT,最后再进行 fftshift
    MatrixXcd S(nrn(), nan1());  // 用于存储距离向FFT结果
    MatrixXcd s_hui_shift = fftshift(s_hui);   // 对 s_hui 进行 fftshift(沿行方向,每列单独处理)
    const int _nan1 = nan1();
    const int _nrn = nrn();
    const double _fs = fs();
    const double _gama = gama();
    for (int j = 0; j < _nan1; j++) {           // 对每一列分别做FFT处理
        VectorXcd col = s_hui_shift.col(j);    // 提取第 j 列信号(长度 nrn())
        VectorXcd col_fft = fft(col);          // 对该列信号进行 FFT
        S.col(j) = fftshift(col_fft);          // 对 FFT 结果再进行 fftshift(保证频谱中心对应零频)
    }
    
    // 脉冲压缩处理
    // 构造频率向量 f_range:(-nrn()/2+1 : nrn()/2)' / nrn() * fs()
    VectorXd f_range(_nrn);
    for (int i = 0; i < _nrn; i++) {   
         f_range(i) = ((i - _nrn / 2.0 + 1.0) / _nrn) * _fs;  // 生成频率向量
    }
    // 构造匹配滤波器 Hr = exp(1j*pi*(f_range^2)/gama())
    VectorXcd Hr(_nrn);
    for (int i = 0; i < _nrn; i++) {
        Hr(i) = exp(I_complex * PI * (f_range(i) * f_range(i)) / _gama);
    }
    // 频域脉冲压缩:S2 = S .* Hr(对每行元素进行相乘,做行向量广播)
    MatrixXcd S2 = S.array() * Hr.replicate(1, nan1()).array();
    
    // 将 S2 转回时域:s_pc = fftshift(ifft(fftshift(S2),[],1),1)
    MatrixXcd s_pc(nrn(), nan1());
    // 对 S2 每列分别进行 ifft(和 FFT 类似的操作)
    MatrixXcd S2_shift = fftshift(S2);
    for (int j = 0; j < _nan1; j++)  {
        VectorXcd col = S2_shift.col(j);  
        VectorXcd col_ifft = ifft(col);
        s_pc.col(j) = fftshift(col_ifft);
    }
    
    // 多普勒处理
    // 多普勒相位补偿公式:exp(1i*2*pi*fr*2*Vr()*(1:nan1())/prf()/c)
    // 先构造慢时间轴向量 slow_time,取值为 1,2,...,nan1()
    RowVectorXd slow_time(nan1());
    for (int j = 0; j < _nan1; j++)  {
        slow_time(j) = j + 1;
    }
    // 计算常数因子:-2*Vr()/(prf()*c) (Vr>0 = approaching, Doppler positive)
    double factor = -2.0 * Vr() / (prf() * c);
    // 利用 fr(nrn() x 1 向量)与 slow_time(1 x nan1() 向量)的外积构造矩阵 M,其尺寸为 nrn() x nan1()
    MatrixXd M_outer = fr * slow_time;
    // 计算多普勒补偿相位矩阵:每个元素 exp(1i*2*pi*factor*M_outer(i,j))
    MatrixXcd doppler_phase = (I_complex * 2.0 * PI * factor * M_outer.array()).exp();
    // 对 S2 进行多普勒补偿:逐元素相乘
    MatrixXcd s_bu = S2.array() * doppler_phase.array();
    
    // 将 s_bu 转回时域:s_buT = fftshift(ifft(fftshift(s_bu),[],1),1)
    MatrixXcd s_buT(nrn(), nan1());
    MatrixXcd s_bu_shift = fftshift(s_bu);
    for (int j = 0; j < _nan1; j++)  {
        VectorXcd col = s_bu_shift.col(j);
        VectorXcd col_ifft = ifft(col);
        s_buT.col(j) = fftshift(col_ifft);
    }
    
    // 方位向FFT处理
    // 对每个距离门(即 s_buT 的每一行)进行慢时间FFT,转换到多普勒频域
    // MatrixXcd F(nrn(), nan1());  // 用于存储最终的距离-多普勒矩阵
    for (int i = 0; i < _nrn; i++)  {
        VectorXcd row_signal = s_buT.row(i).transpose();   // 提取第 i 行(慢时间信号),注意需转换为列向量进行 FFT
        VectorXcd row_fft = fft(row_signal);  // 计算 FFT
        row_fft = fftshift(row_fft);          // 对 FFT 结果进行 fftshift,保证频谱居中
        F.row(i) = row_fft.transpose();   // 将结果转置后存入 F 的第 i 行
    }
    
    // 标度生成与结果保存
    // 生成距离标度 xi = (-nrn()/2:nrn()/2-1) / fs() * c/2 + Rs() (centered)
    VectorXd xi(nrn());
    for (int i = 0; i < _nrn; i++)  {
        xi(i) = ((i - _nrn / 2.0) / _fs) * (c / 2.0) + Rs();
    }
    // 生成速度标度 yi = (-nan1()/2:nan1()/2-1) * prf()*c/fc()/2/nan1() (centered, matches fftshifted F)
    const double _prf = prf();
    const double _fc = fc();
    VectorXd yi(nan1());
    for (int j = 0; j < _nan1; j++) {
        yi(j) = (j - _nan1 / 2.0) * _prf * c / _fc / 2.0 / _nan1;
    }
    
    // F: MatrixXcd, 维度 nrn×nan1, 距离-多普勒图(复数,含随机相位补偿)
    saveMatrix("04_processing_Case2_距离多普勒图_rdmap.dat",F);

}




// 模式3:传统脉冲压缩+速度估计处理(带解模糊)
inline void chuli_Case3(const MatrixXcd& Radar_Sig,MatrixXcd& F)  {

    MatrixXcd x = Radar_Sig;  // 将全局 Radar_Sig 赋值给局部变量 x(尺寸:nrn() x nan1())
    VectorXd tay = kaiser(nrn(), CFG.getDouble("processing.kaiser_beta", 8));

    // 脉冲压缩处理(加窗优化)
    // 定义 lx0 矩阵nrn() x nan1(),用于存储每个脉冲经 FFT 及匹配滤波后的结果,
    MatrixXcd lx0(nrn(), nan1());
    const int _nan1 = nan1();
    const int _nrn = nrn();
    const double _gama = gama();
    // 对于每个脉冲(列),执行距离向 FFT、加窗及匹配滤波操作
    for (int n = 0; n < _nan1; n++) {
        VectorXcd col = x.col(n);          // 提取第 n 列(一个脉冲)的信号,尺寸 nrn() x 1
        VectorXcd col_shifted = fftshift(col);  // 对列信号进行 fftshift(沿行方向)
        VectorXcd col_fft = fft(col_shifted);    // 对移位后的信号做 FFT(Module0.h 中定义的 fft 仅对向量操作)
        VectorXcd col_fft_shift = fftshift(col_fft);  // 再次进行 fftshift 保证中心化
        VectorXcd H(_nrn);
        for (int i = 0; i < _nrn; i++) {
            H(i) = exp(I_complex * PI * pow(fr(i), 2) / _gama);  // 构造匹配滤波器:exp(1j*pi*(fr^2)/gama())
        }
        // 加窗及脉冲压缩:逐元素相乘
        // 注意:tay 为实数向量,需转成 complex<double>复数进行乘法
        VectorXcd col_proc = col_fft_shift.array() * tay.array().cast<complex<double> >() * H.array();
        lx0.col(n) = col_proc;   // 将处理后的结果存入 lx0 的第 n 列
    }
    
    // 对 lx0 每列执行 IFFT 操作以转回时域
    for (int n = 0; n < _nan1; n++) {
        VectorXcd col = lx0.col(n);
        VectorXcd col_shift = fftshift(col);
        VectorXcd col_ifft = ifft(col_shift);
        VectorXcd col_out = fftshift(col_ifft);
        lx0.col(n) = col_out;
    }
    
    // 速度估计(包络对齐法)    
    // 对 lx0 的每个脉冲(每列)检测峰值位置(即最大幅值所在的行索引)
    // 生成 rols 向量,长度为 nan,存储每列峰值位置(索引采用 0-indexed,但用于估计时与 MATLAB() 结果近似)
    VectorXi rols(_nan1);    // 用于存储每列峰值位置 实整数向量
    for (int n = 0; n < _nan1; n++) {
        int idx_max = 0;
        double max_val = 0.0;
        for (int i = 0; i < _nrn; i++) {
            double abs_val = abs(lx0(i, n));
            if (abs_val > max_val) {
                max_val = abs_val;
                idx_max = i;
            }
        }
        rols(n) = idx_max;
    }
    
    // 计算速度估计值 v_est
    // MATLAB() 公式: v_est = (sum(rols(1:nan1()/2)) - sum(rols(nan1()/2+1:nan1())))/(nan1()/2) * c * prf() / (nan1() * fs())
    const double _fs = fs();
    const double _prf3 = prf();
    const double _fc3 = fc();
    const double _prt3 = prt();
    const double _Rs3 = Rs();
    double sum_first = 0.0, sum_second = 0.0;
    int half_nan = _nan1 / 2;
    for (int n = 0; n < half_nan; n++) {
        sum_first += rols(n);
    }
    for (int n = half_nan; n < _nan1; n++) {
        sum_second += rols(n);
    }
    double v_est = ((sum_first - sum_second) / static_cast<double>(half_nan)) * c * _prf3 / (_nan1 * _fs);
    
    // 速度解模糊处理
    // 计算最大不模糊速度 v1_max = c/(2*fc()*prt())
    double v1_max = c / (2.0 * _fc3 * _prt3);
    
    // 计算模糊周期数 NUM = floor((v_est + v1_max/2)/v1_max)
    int NUM = static_cast<int>(floor((v_est + v1_max / 2.0) / v1_max));
    if (NUM < 1) {  NUM = 0;  }   // 修正 NUM 为非负整数
    
    // 多普勒补偿(相位校正)
    // 构造用于多普勒补偿的频率向量 fange  MATLAB() 中: fange = (-nrn()/2:nrn()/2-1).'./nrn() * fs()
    VectorXd fange(_nrn);
    for (int i = 0; i < _nrn; i++) {
        fange(i) = ((i - _nrn / 2.0) / _nrn) * _fs;
    }
    
    MatrixXcd s1(_nrn, _nan1);    // 定义 s1 矩阵,存储多普勒补偿后的信号,尺寸:nrn() x nan1()

    for (int m = 0; m < _nan1; m++) {      // 对每个脉冲 m(0-indexed,对应 MATLAB() 中 m=1...nan)进行补偿
        VectorXcd lx1 = lx0.col(m);   // 提取当前脉冲信号 lx1(nrn() x 1列向量)
        VectorXcd lx1_shift = fftshift(lx1);  // 对 lx1 进行 fftshift -> FFT -> fftshift(与Case2类似)
        VectorXcd lx2 = fft(lx1_shift);
        lx2 = fftshift(lx2);

        double current_time = (m + 1) * _prt3;  // 计算当前时刻,注意 MATLAB() 中 m 为 1-indexed,此处使用 (m+1)*prt()
        
        // 构造多普勒相位补偿项:phase_comp = exp(-1j * 4*pi * fange * v_est/c * current_time)
        VectorXcd phase_comp(_nrn);
        for (int i = 0; i < _nrn; i++) {
            phase_comp(i) = exp(-I_complex * 4.0 * PI * fange(i) * v_est / c * current_time);
        }
        
        VectorXcd lx2_comp = lx2.array() * phase_comp.array();  // 对 lx2 做逐元素相乘,得到补偿后的频域信号
        
        // 对补偿后的信号做 ifftshift -> ifft -> fftshift 转回时域
        VectorXcd lx2_comp_shift = fftshift(lx2_comp);
        VectorXcd lx3 = ifft(lx2_comp_shift);
        lx3 = fftshift(lx3);
        
        s1.col(m) = lx3;   // 将补偿后信号存入 s1 的第 m 列
    }
    
    // 构建距离-多普勒矩阵
    VectorXd xi(_nrn);
    for (int i = 0; i < _nrn; i++) {
        xi(i) = ((i - _nrn / 2.0) / _fs * c / 2.0) + _Rs3;  // 构建距离标度 xi: xi = [-nrn()/2:nrn()/2-1].'/fs()*c/2 + Rs()
    }
    
    // 构建速度标度 dv:   ((-nan1()/2 : nan1()/2-1)) * c/2/prt()/fc()/nan1() + NUM*(c/2/prt()/fc())
    VectorXd dv(_nan1);
    for (int j = 0; j < _nan1; j++) {
        dv(j) = (((j - _nan1 / 2.0) * c) / (2.0 * _prt3 * _fc3 * _nan1)) + NUM * (c / (2.0 * _prt3 * _fc3));
    }
    
    // 构造相位补偿矩阵
    // 首先生成频率向量 freq(覆盖 LFM 带宽内的频率分布),MATLAB() 中:freq = linspace(fc() - B()/2, fc() + B()/2, nan1());
    VectorXd freq_vec = linspace(_fc3 - B() / 2.0, _fc3 + B() / 2.0, _nan1);
    // MatrixXcd F(nrn(), nan1());    // 定义 F 矩阵,用于存储最终的距离-多普勒矩阵,尺寸:nrn() x nan1()

    // 外层循环:遍历每个距离门 g(MATLAB() 中 g=1...nrn())
    for (int g = 0; g < _nrn; g++) {
        RowVectorXcd Temp = s1.row(g); // 提取当前距离门对应的慢时间数据,行向量 Temp(1 x nan)
        // 内层循环:遍历每个脉冲 k,k 从 0 到 nan1()-1
        for (int k = 0; k < _nan1; k++) {
            // 计算补偿系数中的标量因子: (k/nan1() + NUM - 1/2)
            double factor_val = static_cast<double>(k) / static_cast<double>(_nan1) + NUM - 0.5;
            RowVectorXcd W(_nan1);  // 构造补偿向量 W,长度为 nan1()
            for (int j = 0; j < _nan1; j++) {
                // 对于每个脉冲 j(0-indexed),构造指数项:
                // exponent = 2*pi * (freq_vec(j)/fc()) * factor_val * j
                double exponent = 2.0 * PI * (freq_vec(j) / _fc3) * factor_val * j;
                W(j) = exp(-I_complex * exponent);
            }
            
            // 计算当前距离门和速度单元对应的 F(g,k) 值:  F(g,k) = sum( W .* Temp )
            complex<double> sum_val(0.0, 0.0);
            for (int j = 0; j < _nan1; j++) {
                sum_val += W(j) * Temp(j);
            }
            F(g, k) = sum_val;
        }
    }
    
    // F: MatrixXcd, 维度 nrn×nan1, 距离-多普勒图(复数,传统脉压+速度估计解模糊)
    saveMatrix("04_processing_Case3_距离多普勒图_rdmap.dat", F);

}


// 模式4:改进型脉冲压缩处理(带二次相位补偿与速度解模糊)
inline void chuli_Case4(const MatrixXcd& Radar_Sig,MatrixXcd& F)  {
    MatrixXcd x = Radar_Sig;            // 加载含干扰的原始信号 x (nrn() x nan1())
    VectorXd tay = kaiser(nrn(), CFG.getDouble("processing.kaiser_beta", 8));

    const int _nan1 = nan1();
    const int _nrn = nrn();
    const double _gama = gama();
    const double _fs = fs();
    const double _Rs = Rs();
    const double _fc = fc();

    // 脉冲压缩处理(带窗函数优化)
    // 定义 lx0 矩阵存储每个脉冲的距离向 FFT 及匹配滤波结果
    MatrixXcd lx0(nrn(), nan1());
    for (int n = 0; n < _nan1; n++) {
        VectorXcd col = x.col(n);  // 对第 n 个脉冲(列)进行处理
        // 对信号进行 fftshift → FFT → fftshift 操作(保证中心频率对齐)
        VectorXcd col_shifted = fftshift(col);
        VectorXcd col_fft = fft(col_shifted);
        VectorXcd col_fft_shifted = fftshift(col_fft);
        VectorXcd H(_nrn);
        for (int i = 0; i < _nrn; i++) {
            H(i) = exp(I_complex * PI * (fr(i) * fr(i)) / _gama);
        }
        
        // 频域加窗与脉冲压缩:逐元素相乘(注意 tay 为实数向量,需要转换为 complex<double>)
        VectorXcd col_proc = col_fft_shifted.array() *
                             tay.array().cast<complex<double> >() *
                             H.array();
        lx0.col(n) = col_proc;
    }
    
    // 对 lx0 每个脉冲进行 ifft 运算,转回时域
    for (int n = 0; n < _nan1; n++) {
        VectorXcd col = lx0.col(n);
        VectorXcd col_shifted = fftshift(col);
        VectorXcd col_ifft = ifft(col_shifted);
        VectorXcd col_out = fftshift(col_ifft);
        lx0.col(n) = col_out;
    }

    
    // 二次相位补偿(距离徙动校正 RCMC)
    VectorXd xi(_nrn);
    for (int i = 0; i < _nrn; i++) {
        xi(i) = ((i - _nrn / 2.0) / _fs) * (c / 2.0) + _Rs;
    }

    // 载频序列: 优先从全局f加载(由load_case_data从01模块文件加载)
    // 若未加载则回退到线性步进(兜底)
    VectorXd freq(nan1());
    double f_max = f.cwiseAbs().maxCoeff();
    if (f_max > 1e-6) {
        // 已从01模块加载等效载频序列,直接使用
        freq = f;
        cout << "  [Case4] 使用01模块等效载频序列 (max=" << f_max << " Hz)" << endl;
    } else {
        // 兜底: 线性步进(当01模块数据不可用时)
        double delta_f = CFG.getDouble("processing.case4_delta_f", B());
        for (int m = 0; m < _nan1; m++) {
            freq(m) = _fc + (m + 1) * delta_f;
        }
        cout << "  [Case4] 使用线性步进载频序列 (delta_f=" << delta_f << " Hz)" << endl;
    }
    
    // 对每个脉冲 m 进行二次相位补偿
    MatrixXcd s1(nrn(), nan1());
    for (int m = 0; m < _nan1; m++) {
        VectorXcd phase_factor(_nrn);
        for (int i = 0; i < _nrn; i++) {
            double arg = 2.0 * PI * 2.0 * xi(i) * freq(m) / c;
            phase_factor(i) = exp(I_complex * arg);
        }
        s1.col(m) = lx0.col(m).array() * phase_factor.array();
    }
    
    // 多普勒处理(速度解模糊)
    double v_max = c / (2.0 * _fc * prt());
    
    // 通过计算每个距离门上 s1 的平均幅值(对慢时间求均值),检测峰值位置
    VectorXd mean_abs = VectorXd::Zero(_nrn);
    for (int i = 0; i < _nrn; i++) {
        double sum_val = 0.0;
        for (int m = 0; m < _nan1; m++) {
            sum_val += abs(s1(i, m));
        }
        mean_abs(i) = sum_val / _nan1;
    }
    
    // 求出 mean_abs 的最大值所在索引
    int rols = 0;
    double max_val = 0.0;
    for (int i = 0; i < _nrn; i++) {
        if (mean_abs(i) > max_val) {
            max_val = mean_abs(i);
            rols = i;
        }
    }
    int rols_index = rols + 1;
    double NUM_d = floor(((rols_index - _nrn / 2.0) / _nrn) * v_max);
    int NUM = static_cast<int>(NUM_d);
    
    // 构建相位补偿矩阵(使用前面已确定的freq序列)
    VectorXd freq0(nan1());
    for (int j = 0; j < _nan1; j++) {
        freq0(j) = freq(j);
    }
    
    // 对每个距离门 g(0-nrn()-1)进行处理
    for (int g = 0; g < _nrn; g++) {
        RowVectorXcd Temp = s1.row(g);
        // 对每个脉冲 k(0 到 nan1()-1)进行相位补偿
        for (int k = 0; k < _nan1; k++) {
            double A = static_cast<double>(k) / _nan1 + NUM - 0.5;
            complex<double> sum_val(0.0, 0.0);
            // 对于每个脉冲 j(0 到 nan1()-1),构造补偿向量并进行相干积累
            for (int j = 0; j < _nan1; j++) {
                double exponent = 2.0 * PI * (freq0(j) / _fc) * A * j;
                complex<double> W_val = exp(-I_complex * exponent);
                sum_val += W_val * Temp(j);
            }
            F(g, k) = sum_val;
        }
    }
    
    // F: MatrixXcd, 维度 nrn×nan1, 距离-多普勒图(复数,改进脉压+二次相位补偿解模糊)
    saveMatrix("04_processing_Case4_距离多普勒图_rdmap.dat", F);
}





// 模式5:复合处理(跳频+随机相位联合补偿)
inline void chuli_Case5(const MatrixXcd& Radar_Sig,MatrixXcd& F)  {

    // 联合下变频与相位补偿,消除跳频调制和随机相位调制影响
    // 利用全局 Radar_Sig (nrn()×nan),载频向量 f(nan×1,需转置为 1×nan)
    // 与距离向时间向量 tnrn (nrn()×1) 生成广播矩阵,计算下变频因子:
    // 对于每个 (i,j) 元素: exp(-j*2π*f(j)*tnrn(i))
    const int _nan1 = nan1();
    const int _nrn = nrn();
    const double _B = B();
    const double _fs = fs();
    const double _gama = gama();
    const double _prf = prf();
    const double _Vr = Vr();

    // 高精度下变频矩阵:逐列用数字载波方法计算 exp(-j*2*PI*tnrn(i)*f(j))
    const double _Tstart = Tstart();
    MatrixXcd expMat(_nrn, _nan1);
    for (int j = 0; j < _nan1; ++j) {
        expMat.col(j) = precise_expj_2pi_array(-f(j), tnrn.array(), _fs, _Tstart);
    }
    MatrixXcd s_hui = Radar_Sig.array() * expMat.array();   // 跳频下变频:逐元素乘以 expMat
    
    // 随机相位补偿:phi1 为 nan×1 向量,转置为 1×nan 后取共轭,再广播到 (nrn()×nan)
    MatrixXcd phi_rep = phi1.transpose().conjugate().replicate(nrn(), 1);
    s_hui = s_hui.array() * phi_rep.array();

    // 频域滤波与脉冲压缩
    VectorXd win(_nrn);
    for (int i = 0; i < _nrn; i++) {
        win(i) = (abs(fr(i)) <= (_B / 2.0)) ? 1.0 : 0.0;
    }
    
    // 对 s_hui 进行距离向 FFT 处理:对每一列进行 fftshift → FFT → fftshift
    MatrixXcd S_hui(nrn(), nan1());
    for (int j = 0; j < _nan1; j++) {
        VectorXcd col = s_hui.col(j);
        col = fftshift(col);
        col = fft(col);
        col = fftshift(col);
        S_hui.col(j) = col;
    }

    MatrixXcd S = S_hui.array() * win.replicate(1, nan1()).array();
    
     
    MatrixXcd S_hui1(nrn(), nan1());
    for (int j = 0; j < _nan1; j++) {
        VectorXcd col = S.col(j);
        col = fftshift(col);
        col = ifft(col);
        col = fftshift(col);
        S_hui1.col(j) = col;
    }

    // 脉冲压缩处理
    VectorXd f_range(_nrn);
    VectorXcd Hr(_nrn);
    for (int i = 0; i < _nrn; i++) {
        f_range(i) = ((i - _nrn / 2.0 + 1.0) / _nrn) * _fs;
        Hr(i) = exp(I_complex * PI * (f_range(i) * f_range(i)) / _gama);
    }
    // 频域脉冲压缩:S2 = S .* Hr
    MatrixXcd S2 = S.array() * Hr.replicate(1, nan1()).array();
    
    // 转回时域:对 S2 的每一列进行 ifft(前后 fftshift)
    MatrixXcd s_pc(nrn(), nan1());
    for (int j = 0; j < _nan1; j++) {
        VectorXcd col = S2.col(j);
        col = fftshift(col);
        col = ifft(col);
        col = fftshift(col);
        s_pc.col(j) = col;
    }
    
    // 多普勒补偿
    MatrixXcd doppler_phase(nrn(), nan1());
    for (int j = 0; j < _nan1; j++) {
        double t_factor = (j + 1) / _prf;
        for (int i = 0; i < _nrn; i++) {
            double phase_arg = -2.0 * PI * fr(i) * 2.0 * _Vr * t_factor / c;
            doppler_phase(i, j) = exp(I_complex * phase_arg);
        }
    }
    // 多普勒相位补偿:s_bu = S2 .* doppler_phase
    MatrixXcd s_bu = S2.array() * doppler_phase.array();
    
    // 转回时域:对 s_bu 的每一列做 ifft(前后 fftshift)
    MatrixXcd s_buT(nrn(), nan1());
    for (int j = 0; j < _nan1; j++) {
        VectorXcd col = s_bu.col(j);
        col = fftshift(col);
        col = ifft(col);
        col = fftshift(col);
        s_buT.col(j) = col;
    }
    
    // 距离-多普勒耦合补偿
    VectorXd xi = tnrn * (c / 2.0);
    MatrixXcd lx6(nrn(), nan1());
    for (int m = 0; m < _nan1; m++) {
        // 对于每个脉冲 m,计算补偿相位:exp(j*2π*2*xi/c * f(m))
        VectorXcd phase_factor(_nrn);
        for (int i = 0; i < _nrn; i++) {
            double arg = 2.0 * PI * 2.0 * xi(i) * f(m) / c;
            phase_factor(i) = exp(I_complex * arg);
        }
        lx6.col(m) = s_buT.col(m).array() * phase_factor.array();
    }
    
    const double _fc = fc();
    const double _prt = prt();
    // 速度解模糊处理 (Vr>0 = approaching)
    double v_max = _prf * c / (2.0 * _fc);
    int NUM = static_cast<int>(floor((_Vr + v_max / 2.0) / v_max));
    
    VectorXd dv(nan1());
    for (int j = 0; j < _nan1; j++) {
        dv(j) = ((j - _nan1 / 2.0) * (c / (2.0 * _prt * _fc * _nan1))) + NUM * (c / (2.0 * _prt * _fc));
    }
    
    // 多普勒维聚焦处理
    for (int g = 0; g < _nrn; g++) {
        RowVectorXcd Temp = lx6.row(g);
        for (int k = 0; k < _nan1; k++) {
            double A = static_cast<double>(k) / _nan1 - 0.5 + NUM;
            complex<double> sum_val(0.0, 0.0);
            for (int j = 0; j < _nan1; j++) {
                double exponent = 2.0 * PI * (f(j) / _fc) * A * j;
                complex<double> W_val = exp(-I_complex * exponent);
                sum_val += W_val * Temp(j);
            }
            F(g, k) = sum_val;
        }
    }
    
    // F: MatrixXcd, 维度 nrn×nan1, 距离-多普勒图(复数,跳频+随机相位联合补偿解模糊)
    saveMatrix("04_processing_Case5_距离多普勒图_rdmap.dat", F);

}


// 模式6:时频干扰解耦(Tsallis交叉熵二值分割时频图)
inline void chuli_Case6(const MatrixXcd& Radar_Sig) {

    int nrn = Radar_Sig.rows();    // 每个脉冲的采样点数，即2048
    int nan1 = Radar_Sig.cols();   // 脉冲数量，即64
    VectorXd gaojiepu_idx_1 = VectorXd::Zero(nan1);

    MatrixXcd pulses_jammingsignal = MatrixXcd::Zero(nrn, nan1);
    MatrixXcd pulses_Targetsignal = MatrixXcd::Zero(nrn, nan1);

    int gaojiepu_count = 0;
    double threshold_sum = 0;
    for (int i = 0; i < nan1; ++i) {
        VectorXcd temp_data = Radar_Sig.col(i);

        JamTarDiviResult divi_result = jamTarDivi(temp_data);

        pulses_jammingsignal.col(i) = divi_result.jammingsignal;
        pulses_Targetsignal.col(i) = divi_result.targetsignal;

        gaojiepu_idx_1(i) = divi_result.gaojiepu_idx;
        if (divi_result.gaojiepu_idx == 1) gaojiepu_count++;
        threshold_sum += divi_result.threshold;
    }

    double input_energy = Radar_Sig.cwiseAbs().matrix().norm();
    double jam_energy = pulses_jammingsignal.cwiseAbs().matrix().norm();
    double target_energy = pulses_Targetsignal.cwiseAbs().matrix().norm();
    double avg_threshold = threshold_sum / nan1;
    double ISR_dB = (jam_energy > 1e-15) ? 20.0 * log10(target_energy / jam_energy) : 0.0;

    cout << "  Case6诊断: 输入Frobenius范数=" << input_energy
         << " 干扰分离=" << jam_energy
         << " 目标分离=" << target_energy
         << " ISR=" << ISR_dB << "dB"
         << " 平均阈值=" << avg_threshold
         << " 高阶谱脉冲数=" << gaojiepu_count << "/" << nan1 << endl;

    saveMatrix("04_processing_Case6_分离干扰信号_jam_sig.dat", pulses_jammingsignal);
    saveMatrix("04_processing_Case6_分离目标信号_target_sig.dat", pulses_Targetsignal);
    // gaojiepu_idx_1: VectorXd, 维度 nan1×1, 每个脉冲的干扰解耦成功标志(实数,0=正常,1=高阶谱)
    saveVector("04_processing_Case6_解耦标志_decouple_flag.dat", gaojiepu_idx_1);

}




/********************************************** 信号处理函数 结束 ************************************************/

/*
*  模式 1:跳频信号处理(带速度补偿与解模糊)
*    过程上,首先加载跳频序列并计算脉冲重复间隔.接着进行动态下变频处理,通过 exp(-j2πf(t)・t) 抵消时变载频调制.之后构建带宽
*  限制窗进行频域滤波.在距离向处理中,经过 FFT、滤波、IFFT 操作.脉冲压缩时构造匹配滤波器并进行频域脉冲压缩和时域转换.多普
*  勒补偿和距离 - 多普勒耦合补偿分别通过相应公式完成.计算模糊周期数进行速度解模糊处理,最后进行多普勒聚焦处理,保存结果并可视
*  化.
*    原理是通过多域补偿解决速度模糊与跳频耦合问题,利用信号处理手段恢复信号特征.
*    注意跳频序列验证、脉冲压缩验证和速度解模糊验证,可通过加窗处理、并行加速和动态滤波优化性能.

*  模式 2:固定载频信号处理(含随机相位补偿)
*    先计算脉冲重复间隔,下变频处理消除固定载频调制,随机相位补偿通过共轭运算抵消发射端随机相位扰动.距离向处理构建频率轴并进行 
*  FFT.脉冲压缩构造匹配滤波器进行频域和时域转换.多普勒处理进行相位补偿并转回时域.方位向 FFT 处理将慢时间信号转为多普勒频率域.
*  生成距离和速度标度,保存结果并可视化.
*    原理是通过相位补偿消除主动干扰,传统脉冲压缩提升距离分辨率,多普勒补偿校正运动目标相位.
*    可通过加窗优化、非均匀采样补偿和动目标显示优化性能,调试时注意相位补偿验证、多普勒谱纯度检测和速度标度验证.

*  模式 3:传统脉冲压缩 + 速度估计(带解模糊)
*    初始化加载信号,生成凯瑟窗抑制旁瓣,构建频率向量.脉冲压缩时对每个脉冲进行 FFT、加窗和频域脉冲压缩,再转回时域并显示结果.
*  速度估计采用包络对齐法,计算模糊周期数进行速度解模糊处理.多普勒补偿通过相位校正完成,构建距离 - 多普勒矩阵并保存结果可视化.
*    原理是传统脉冲压缩实现高分辨率测距,结合峰值检测与相位补偿解决速度模糊问题.
*    调试时注意速度估计验证、模糊次数验证和距离徙动检查.

*  模式 4:改进型脉冲压缩(带二次相位补偿与速度解模糊)
*    初始化加载信号,生成凯瑟窗和频率向量.脉冲压缩同模式 3,显示脉压结果.二次相位补偿通过 exp(j2π・2ΔR・f/c) 校正距离徙动.多普
*  勒处理计算速度模糊参数,构建相位补偿矩阵进行相干积累.保存结果并可视化.
*    原理是在传统脉冲压缩基础上增加距离徙动补偿和精细速度解模糊.
*    注意凯瑟窗参数、最大不模糊速度和速度模糊周期数,通过性能指标评估效果.

*  模式 5:复合处理(跳频 + 随机相位联合补偿)
*    加载跳频和随机相位序列,计算脉冲重复间隔.联合下变频与相位补偿消除发射端跳频和随机相位调制影响.频域滤波和脉冲压缩按常规操作
*  进行.多普勒补偿和距离 - 多普勒耦合补偿解决相应问题.计算模糊周期数和构建速度轴进行速度解模糊,多普勒维聚焦处理通过相干积累
*  实现.保存结果并可视化.
*    原理是处理复杂干扰场景,通过联合频域 - 时域补偿消除双重调制影响.
*    注意跳频对距离分辨率和速度模糊的影响,可验证随机相位补偿效果. 
*
*  模式 6:时频干扰解耦(Tsallis交叉熵二值分割时频图)
*    过程上,首先初始化存储矩阵用于分离信号和时频图.遍历每个脉冲数据,通过JamTarDivi函数执行核心解耦操作.该函数基于短时傅里叶变换
*  生成时频图,采用Tsallis交叉熵准则进行二值分割,区分干扰和目标的时频能量分布.分离后的信号分通道存储,时频图通过行列变换重组为连续
*  时频平面.最终保存干扰信号、目标信号及其时频表征,并返回解耦成功标志.
*    原理是利用非广延熵的几何特性实现时频域精确分割,通过时频能量聚类分离信号成分.Tsallis熵的参数q控制分割灵敏度,二值化后通过逆
*  STFT重构时域信号.
*    注意时频分辨率设置(256点STFT)、交叉熵阈值选取和干扰形态适配性.可通过优化时频窗函数、并行脉冲处理加速,验证时频图分割效果可
*  通过能量比计算和视觉校验.
*/

#endif // MODULE3_H    

