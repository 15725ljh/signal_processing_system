// 本模块包含了所有的常数参数设置、全局变量声明、内联函数等内容
#ifndef MODULE0_H
#define MODULE0_H
#include "parameters.h"                          // 全局参数定义(fc, Tp, B等) + Config.h配置管理器
#include <iostream>                              // 标准输入输出(cout, cerr, endl)
#include <cmath>                                 // 数学函数(sin, cos, exp, sqrt, pow, PI等)
// 第一类修正贝塞尔函数 I0(x) — 泰勒级数展开,替代 Boost 依赖
inline double bessel_i0(double x)
{
    double y = x * x / 4.0;
    double sum = 1.0, term = 1.0;
    for (int k = 1; k <= 60; ++k) {
        term *= y / (static_cast<double>(k) * k);
        sum += term;
        if (term < 1e-15 * sum) break;
    }
    return sum;
}
#include <Eigen/Dense>                            // Eigen线性代数库(Matrix, Vector, Array等)

#include <vector>                                // STL动态数组
#include <complex>                               // 复数类型(complex<double>)
#include <algorithm>                             // STL算法(min, max, sort, shuffle等)
#include <numeric>                               // STL数值算法(iota, accumulate等)
#include <fstream>                               // 文件输入输出(ifstream, ofstream)
#include <string>                                // STL字符串
#include <random>                                // C++11随机数生成器(mt19937, uniform_int_distribution等)
#include <sstream>                               // 字符串流(ostringstream,用于拼接输出)
#include <filesystem>                            // C++17文件系统(目录创建等)
#include "fftw3.h"                               // FFTW3快速傅里叶变换库

namespace std_fs = std::filesystem;              // filesystem命名空间别名

// Windows UTF-8 路径支持: 解决中文文件名在 GBK 系统下乱码的问题
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace _fs_detail {
inline std_fs::path _u8path(const std::string& s) {
    if (s.empty()) return std_fs::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return std_fs::path(std::move(w));
}
}
#define U8PATH(s) _fs_detail::_u8path(s)
#else
#define U8PATH(s) (s)
#endif

#ifndef OUTPUT_DIR
#define OUTPUT_DIR "../output/"                   // 统一输出目录(相对于可执行文件所在目录)
#endif

inline void ensure_output_dir() {
    try { std_fs::create_directories(OUTPUT_DIR); } catch (...) {}
}



/*********************************************** 内联工具函数 开始 **********************************************/

/*
*  辅助类型判断模板:判断是否为复数类型
*/
template<typename T>
struct is_complex_type : false_type {};

template<typename T>
struct is_complex_type<complex<T>> : true_type {};


/* 
*  保存向量为标准的 .dat 文件(纯数值数据,每个元素单独占一行,无论原来是行向量还是列向量)
*  重载版本1:适用于实向量
*  当向量的标量类型不是复数时,该模板生效
*/
template <typename Derived, typename enable_if<!is_complex_type<typename Derived::Scalar>::value, int>::type = 0>
inline void saveVector(const string& filename, const MatrixBase<Derived>& data) {
    if (data.rows() != 1 && data.cols() != 1) {
        cerr << "错误:输入的数据不是向量类型." << endl;
        return;
    }
    
    string full_path = string(OUTPUT_DIR) + filename;
    ensure_output_dir();
    
    ofstream file(U8PATH(full_path));
    if (!file.is_open()) {
        cerr << "错误:无法打开文件 " << full_path << " :正被写入中." << endl;
        return;
    }
    
    for (int i = 0; i < data.size(); ++i) {
        file << data.derived().coeff(i) << endl;
    }
    
    file.close();
}


/* 
*  保存向量为标准的 .dat 文件(纯数值数据,每个元素单独占一行,无论原来是行向量还是列向量)
*  重载版本2:适用于复向量,输出格式为:实部 虚部(以空格分隔)
*  当向量的标量类型为复数时,该模板生效
 */
template <typename Derived, typename enable_if<is_complex_type<typename Derived::Scalar>::value, int>::type = 0>
inline void saveVector(const string& filename, const MatrixBase<Derived>& data) {
    if (data.rows() != 1 && data.cols() != 1) {
        cerr << "错误:输入的数据不是向量类型." << endl;
        return;
    }
    
    string full_path = string(OUTPUT_DIR) + filename;
    ensure_output_dir();
    
    ofstream file(U8PATH(full_path));
    if (!file.is_open()) {
        cerr << "错误:无法打开文件 " << full_path << " :正被写入中." << endl;
        return;
    }
    
    for (int i = 0; i < data.size(); ++i) {
        auto elem = data.derived().coeff(i);
        file <<"( " << elem.real() << " + " << elem.imag() << " j)" << endl;
    }
    
    file.close();
}


/* 
*  保存复矩阵为标准的 .dat 文件:
*  第一行写入矩阵的行数和列数(以空格分隔),随后每一行对应矩阵的一个元素数据；
*  每个复数以“(实部 + 虚部j)”的形式写入.
*/ 
template <typename T>
inline void saveMatrix(const string& filename, const Matrix<complex<T>, Dynamic, Dynamic>& data) {
    string full_path = string(OUTPUT_DIR) + filename;
    ensure_output_dir();
    
    ofstream file(U8PATH(full_path));
    if (!file.is_open()) {
        cerr << "错误:无法打开文件 " << full_path << " :正被写入中." << endl;
        return;
    }
    file << data.rows() << " " << data.cols() << endl;
    for (int i = 0; i < data.rows(); ++i) {
        for (int j = 0; j < data.cols(); ++j) {
            file << "( " << data(i, j).real() << " + " << data(i, j).imag() << " j)" << endl;
        }
    }
    file.close();
}

/* 
*  保存实矩阵为标准的 .dat 文件:
*  第一行写入矩阵的行数和列数(以空格分隔),随后每一行对应矩阵的一个元素数据；
*/ 
template <typename T>
inline void saveMatrix(const string& filename, const Matrix<T, Dynamic, Dynamic>& data) {
    string full_path = string(OUTPUT_DIR) + filename;
    ensure_output_dir();
    
    ofstream file(U8PATH(full_path));
    if (!file.is_open()) {
        cerr << "错误: 无法打开文件 " << full_path << " : 正被写入中." << endl;
        return;
    }
    file << data.rows() << " " << data.cols() << endl;
    for (int i = 0; i < data.rows(); ++i) {
        for (int j = 0; j < data.cols(); ++j) {
            file << data(i, j) << endl;
        }
    }
    file.close();
}


/*  
 *  fft 函数:实现快速傅里叶变换(FFT)
 *  输入:
 *    input - 输入复数向量
 *  输出:
 *    result - 输出复数向量
 *  返回值:
 *    result - 输出复数向量
 */
inline VectorXcd fft(const VectorXcd& input) {
    int n = input.size();
    fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n);
    fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n);

    for (int i = 0; i < n; ++i) {
        in[i][0] = input(i).real();
        in[i][1] = input(i).imag();
    }

    fftw_plan p = fftw_plan_dft_1d(n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(p);

    VectorXcd result(n);
    for (int i = 0; i < n; ++i) {
        result(i) = complex<double>(out[i][0], out[i][1]);
    }

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);

    return result;
}


/*  
 *  ifft 函数:实现快速逆傅里叶变换(IFFT)
 *  输入:
 *    input - 输入复数向量
 *  输出:
 *    result - 输出复数向量
 *  返回值:
 *    result - 输出复数向量
 */
inline VectorXcd ifft(const VectorXcd& input) {
    int n = input.size();
    fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n);
    fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * n);

    for (int i = 0; i < n; ++i) {
        in[i][0] = input(i).real();
        in[i][1] = input(i).imag();
    }

    fftw_plan p = fftw_plan_dft_1d(n, in, out, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(p);

    VectorXcd result(n);
    for (int i = 0; i < n; ++i) {
        result(i) = complex<double>(out[i][0] / n, out[i][1] / n);
    }

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);

    return result;
}


/*
 *  butter 函数:设计数字低通 Butterworth 滤波器,实现 MATLAB 中 [b, a] = butter(order, cutoff) 功能(仅低通)
 *  输入:
 *    order - 滤波器阶数(正整数)
 *    cutoff - 归一化截止频率(0～1,1 为 Nyquist 频率)
 *  输出:
 *    b - 分子系数向量(按 z^0, z^-1, ... 排列)
 *    a - 分母系数向量(按 z^0, z^-1, ... 排列,a[0] = 1)
 *  步骤:
 *  1. 预扭曲截止频率(tan(π*cutoff))
 *  2. 计算模拟滤波器极点(s 平面)
 *  3. 双线性变换将模拟极点映射到数字域(z 平面)
 *  4. 根据数字极点计算分母多项式 a(z)
 *  5. 低通滤波器零点在 z = -1,计算分子多项式 b(z)
 *  6. 调整增益使直流(z=1)处增益 H(1)=1
 */
inline void butter(int order, double cutoff, vector<double>& b, vector<double>& a) {
    int n = order;
    // 预扭曲截止频率
    double warped = tan(PI * cutoff);

    // 计算模拟极点
    vector<complex<double>> analog_poles;
    for (int k = 0; k < n; ++k) {
        double theta = PI * (2.0 * k + 1.0 + n) / (2.0 * n);
        complex<double> p = warped * exp(complex<double>(0, theta));
        analog_poles.push_back(p);
    }

    // 双线性变换到数字域
    vector<complex<double>> digital_poles;
    for (int k = 0; k < n; ++k) {
        complex<double> z = (2.0 + analog_poles[k]) / (2.0 - analog_poles[k]);
        digital_poles.push_back(z);
    }

    // 计算分母多项式 a(z)
    vector<complex<double>> a_complex = {1.0};
    for (int k = 0; k < n; ++k) {
        vector<complex<double>> new_poly(a_complex.size() + 1, 0.0);
        for (size_t i = 0; i < a_complex.size(); ++i) {
            new_poly[i] += a_complex[i];
            new_poly[i + 1] -= a_complex[i] * digital_poles[k];
        }
        a_complex = new_poly;
    }
    a.resize(a_complex.size());
    for (size_t i = 0; i < a_complex.size(); ++i) {
        a[i] = a_complex[i].real();
    }
    double a0 = a[0];
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] /= a0;
    }

    // 计算分子多项式 b(z)
    b.resize(n + 1, 0.0);
    for (int i = 0; i <= n; ++i) {
        double binom = 1.0;
        for (int j = 1; j <= i; ++j) {
            binom *= (n - j + 1.0) / j;
        }
        b[i] = binom;
    }

    // 调整增益
    double sum_b = 0.0, sum_a = 0.0;
    for (size_t i = 0; i < b.size(); ++i) sum_b += b[i];
    for (size_t i = 0; i < a.size(); ++i) sum_a += a[i];
    double gain = sum_a / sum_b;
    for (size_t i = 0; i < b.size(); ++i) {
        b[i] *= gain;
    }
}


/*
 *  filter 函数:实现 MATLAB 中 y = filter(b, a, x) 功能,对复数输入信号 x 进行 IIR 滤波
 *  输入:
 *   b - 分子系数向量(double 类型,z^0, z^-1, ... 排列)
 *   a - 分母系数向量(double 类型,z^0, z^-1, ... 排列,a[0] = 1)
 *   x - 输入信号(复数向量)
 *  返回:
 *   y - 输出信号(复数向量,与 x 长度相同)
 *  原理:
 *  根据 IIR 滤波器差分方程 y[n] = (b[0]*x[n] + ... + b[M]*x[n-M]) - (a[1]*y[n-1] + ... + a[N]*y[n-N]) 计算,未定义项为 0
 */
inline VectorXcd filter(const vector<double>& b, const vector<double>& a, const VectorXcd& x) {
    int N = x.size(), nb = b.size(), na = a.size();
    VectorXcd y = VectorXcd::Zero(N);

    for (int n = 0; n < N; ++n) {
        complex<double> acc = 0.0;
        // 计算分子部分
        for (int i = 0; i < nb; ++i) {
            if (n - i >= 0) {
                acc += b[i] * x(n - i);
            }
        }
        // 计算分母部分
        for (int j = 1; j < na; ++j) {
            if (n - j >= 0) {
                acc -= a[j] * y(n - j);
            }
        }
        y(n) = acc;
    }
    return y;
}


/*
*  fftshift 内联函数实现 复数向量版本
*  对于向量版本:对输入向量进行 fftshift 操作(沿着单一维度重新排列)
*/
inline VectorXcd fftshift(const VectorXcd &v) {
    int n = v.size();
    int shift = n / 2; // floor(n/2)
    VectorXcd result(n);
    result.head(n - shift) = v.segment(shift, n - shift);
    result.tail(shift) = v.head(shift);
    return result;
}


/*
*  fftshift 内联函数实现 复数矩阵版本
*  对于矩阵版本:沿着行方向(即每一列独立地)进行 fftshift 操作
*/
inline MatrixXcd fftshift(const MatrixXcd &mat) {
    int rows = mat.rows();
    int shift = rows / 2; // 对于奇数和偶数均取 floor(rows/2)
    MatrixXcd result(mat.rows(), mat.cols());
    result.topRows(rows - shift) = mat.bottomRows(rows - shift);
    result.bottomRows(shift) = mat.topRows(shift);
    return result;
}



/*
*  辅助函数:实现凯瑟窗(Kaiser window)
*  输入: N - 窗口长度
*       beta - 参数(β=0为矩形窗,β=5为典型值)
*  输出:Kaiser 窗向量
*/
inline VectorXd kaiser(int N, double beta)
{
    VectorXd w(N);
    // 计算分母 I0(beta)
    double denom = bessel_i0( beta);
    for (int n = 0; n < N; n++) {
        // 计算归一化距离,取值范围 [-1,1]
        double ratio = (2.0 * n) / (N - 1) - 1.0;
        // 计算 I0(beta*sqrt(1 - ratio^2))
        double arg = beta * sqrt(1 - ratio * ratio);
        w(n) = bessel_i0( arg) / denom;
    }
    return w;
}

/*
*  辅助函数:linspace 实现,生成等间距向量
*  输入: start - 起始值
*       end - 结束值
*       num - 向量长度
*  输出:等间距向量
*/
inline VectorXd linspace(double start, double end, int num)
{
    VectorXd vec(num);
    if(num == 1) {
        vec(0) = start;
    } else {
        double step = (end - start) / (num - 1);
        for (int i = 0; i < num; i++) {
            vec(i) = start + step * i;
        }
    }
    return vec;
}


/*
 * 定义一个函数用于输出跳频序列f和随机相位phi1的某一元素
 */
inline void print_f_phi1(int boxing_mode, const VectorXd& f, const VectorXcd& phi1) {

    if (boxing_mode == 1 || boxing_mode == 5) {
        // 因为只有boxing的case1和case5中定义了f(fre),所以只有这两种情况下才输出
        cout << endl << "在波形定义和使用了跳频序列的情况下:" << endl <<
                "跳频序列f(fre)的某一元素f(32): " << f(32) << endl;         // 跳频序列f(fre)的某一元素
    } else {
        cout << endl << "未定义或使用跳频序列f(fre),不打印信息" << endl;
    }

    if (boxing_mode == 2 || boxing_mode == 5) {
        // 因为只有boxing的case2和case5中定义了phi1(phi),所以只有这两种情况下才输出
        cout << endl << "在波形定义和使用了随机相位的情况下: " << endl <<
                "随机相位phi1(phi)某一元素phi1(32): " << phi1(32) << endl;  // 随机相位phi1(phi)某一元素
    } else {
        cout << endl << "未定义或使用随机相位phi1(phi),不打印信息" << endl;
    }
}


/*
*  提示用户输入特定字符退出程序
*/
inline void promptToExit() {
    cout << "程序运行完毕，自动退出." << endl;
}


/*
*生成高斯白噪噪声
*    输入:
*        signal - 信号向量   
*        snr - 信噪比(dB)
*    返回:
*        noise - 噪声向量
*    原理:
*        信噪比 = 信号能量/噪声能量 = 10log10(信噪比)
*/
inline VectorXcd awgn(const VectorXcd& signal, double snr) {
    // 使用线程本地存储的随机数生成
    thread_local static mt19937 generator{random_device{}()};
    thread_local static normal_distribution<double> dist{0.0, 1.0};

    const double signal_power = signal.cwiseAbs2().sum() / signal.size();
    const double noise_power = signal_power / pow(10.0, snr / 10.0);
    const double noise_std = sqrt(noise_power / 2.0);  // 考虑复数的实虚两部分

    VectorXcd noise(signal.size());
    for (int i = 0; i < signal.size(); ++i) {
        // 为复数生成独立的实部和虚部噪声
        noise[i] = {dist(generator) * noise_std,   
                    dist(generator) * noise_std}; 
    }
    return noise;
}


/*
*   生成归一化汉明窗向量
*   输入: M - 窗长度  输出: 窗向量
*   窗函数 = 0.54 - 0.46 * cos(2 * pi * n / (M - 1))
*/
inline VectorXd generateHammingWindow(int M) {
    VectorXd window(M);
    for (int i = 0; i < M; ++i) {
        window(i) = 0.54 - 0.46 * cos(2 * PI * i / (M - 1));
    }
    window /= window.norm();    // 归一化
    return window;
}


/*
 * 通过Otsu阈值法对时频图进行分割，生成干扰信号的掩模
 * 
 * tfr 短时傅里叶变换（STFT）结果的复数矩阵，维度是STFT_NUM*nrn   (256*2048)
 * jam_tfr     干扰信号的掩模矩阵，与tfr同尺寸，元素值为0或1
 *                    0表示目标信号区域，1表示干扰信号区域        (256*2048)
 */
inline void JamLocated(const MatrixXcd& tfr,MatrixXcd& jam_tfr) {

    int rows = tfr.rows();   // STFT_NUM
    int cols = tfr.cols();   // nrn
    jam_tfr = MatrixXcd::Ones(rows, cols);  // 初始化干扰信号掩模矩阵,不提取任何区域

    MatrixXd abs_TFR = tfr.array().abs();       // 计算tfr的幅度谱
    
    // 计算量化步长delta_P，将幅度谱的动态范围映射到0-255的灰度级
    double min_val = abs_TFR.minCoeff();    // 最小值
    double max_val = abs_TFR.maxCoeff();    // 最大值
    double delta_P = (max_val - min_val) / 255.0;  // 量化步长
    
    MatrixXi TFR_RGB(rows, cols);    // 量化为0-255的灰度图像的幅度谱矩阵
    for (int m = 0; m < rows; m++) {
        for (int n = 0; n < cols; n++) {
            int val = round(abs_TFR(m, n) / delta_P);  // 将幅度谱量化为0-255的灰度值
            TFR_RGB(m, n) = min(255, max(0, val));     // 限制在0-255范围内
        }
    }
    
    int num = rows * cols;          // 计算时频图的总像素数
    vector<double> p(256, 0.0);     // 初始化灰度直方图的概率数组

    for (int m = 0; m < rows; m++) {
        for (int n = 0; n < cols; n++) {
            p[TFR_RGB(m, n)] += 1.0 / num;   // 计算每个灰度级的出现概率,统计直方图
        }
    }
    
    vector<double> vara(256, 0.0);    // 初始化类间方差数组，用于Otsu阈值法
    
    for (int rgb = 0; rgb < 256; rgb++) {  // 遍历所有灰度级，计算类间方差以确定最佳阈值
        double p1 = 0.0;
        for (int j = 0; j <= rgb; j++) {
            p1 += p[j];  // 计算前景（灰度级0到rgb）的概率和
        }
        
        double p2 = 1.0 - p1;  // 计算背景（灰度级rgb+1到255）的概率和

        if (p1 < 1e-10 || p2 < 1e-10) {
            vara[rgb] = 0.0;   // 避免除以0
            continue;
        }
        
        double m1 = 0.0;
        for (int j = 0; j <= rgb; j++) {
            m1 += j * p[j];   // 计算前景的平均灰度
        }
        m1 /= p1;
        
        double m2 = 0.0;
        for (int j = rgb + 1; j < 256; j++) {
            m2 += j * p[j];   // 计算背景的平均灰度
        }
        m2 /= p2;
        
        vara[rgb] = p1 * p2 * pow(m1 - m2, 2);  // 计算类间方差
    }
    
    // 找到类间方差最大的灰度级，作为初步阈值
    auto max_it = max_element(vara.begin(), vara.end());
    int index = distance(vara.begin(), max_it);
    
    index = max(0, index - 10);  // 对阈值进行调整，减去10
    
    for (int m = 0; m < rows; m++) {
        for (int n = 0; n < cols; n++) {
            if (TFR_RGB(m, n) < index) {
                jam_tfr(m, n) = complex<double>(0.0, 0.0);  // 根据阈值生成掩模,选择性提取区域认定干扰区域
            }
        }
    }

}

/*
* 辅助函数：找出非零元素的最后一个索引
*/
inline int findLastNonZero(const vector<int>& vec) {
    for (int i = vec.size() - 1; i >= 0; --i) {
        if (vec[i] != 0) return i;
    }
    return -1; // 没有找到非零元素
}






/*********************************************** 内联工具函数 结束 **********************************************/

/*********************************************** 跨平台兼容函数 **********************************************/

inline void platform_pause() {
#if defined(_WIN32) || defined(_WIN64)
    system("timeout /t 1 /nobreak >nul");
#else
    cout << "继续..." << endl;
#endif
}

#endif // MODULE0_H