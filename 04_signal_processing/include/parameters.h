#ifndef PARAMETERS_H
#define PARAMETERS_H
#include <Eigen/Dense>          // 线性代数库(矩阵/向量运算)
#include <cmath>               // 数学函数(pow, sin, cos, sqrt, floor等)
#include <vector>               // STL动态数组
#include <complex>              // 复数类型支持
#include "Config.h"             // 外部JSON配置文件管理器(单例模式)
// #include <Windows.h>         // Windows平台头文件(跨平台不需要)

#define PI 3.14159265358979323846     // 圆周率常量
#define I_complex complex<double>(0,1) // 虚数单位 i = 0+1j
#define CFG Config::instance()        // 配置管理器单例快捷宏
using namespace Eigen;                // Eigen命名空间(MatrixXcd, VectorXd等)
using namespace std;                  // 标准命名空间(string, vector, cout等)

inline int boxing_mode;       // 波形生成模式编号(1~5),由boxing()函数设置
inline int ganrao_mode;       // 干扰生成模式编号(1~10),由ganrao()函数设置
inline int pulse_index;       // 当前选中的脉冲索引,用于干扰识别
inline int chuli_mode;        // 信号处理模式编号(1~6),由chuli()函数设置

/************************************************ 参数部分 开始 *************************************************/
const double c = 3e8;                    // 光速(m/s),物理常量,不随配置改变

/* --- 10个基础参数:从config.json的system.*路径读取,无外部文件则用默认值 --- */
static inline double _cfg_fc()   { return CFG.getDouble("system.fc",   16e9);  }   // 载波频率(Hz),默认16GHz
static inline double _cfg_Tp()   { return CFG.getDouble("system.Tp",   12e-6); }   // 脉冲宽度(s),默认12μs
static inline double _cfg_B()    { return CFG.getDouble("system.B",    40e6);  }   // 信号带宽(Hz),默认40MHz
static inline double _cfg_prf()  { return CFG.getDouble("system.prf",  10e3);  }   // 脉冲重复频率(Hz),默认10kHz
static inline double _cfg_Vr()   { return CFG.getDouble("system.Vr",   50);    }   // 雷达与目标的相对径向速度(m/s),默认50
static inline double _cfg_Rs()   { return CFG.getDouble("system.Rs",   10000); }   // 场景中心斜距(m),默认10000
static inline double _cfg_wr()   { return CFG.getDouble("system.wr",   608);   }   // 场景距离向宽度(m),默认608
static inline double _cfg_A_RJ() { return CFG.getDouble("system.A_RJ", 10);    }   // 干扰幅度增益(dB),默认10
static inline double _cfg_z_R0() { return CFG.getDouble("system.z_R0", 2000);  }   // 雷达的初始高度(m),默认2000
static inline int    _cfg_nan1() { return CFG.getInt("system.nan1",    64);    }   // 方位向脉冲数(慢时间采样点数),默认64

/* --- 基础参数对外接口函数(各模块统一调用) --- */
static inline double fc()     { return _cfg_fc();   }   // 载波频率(Hz)
static inline double Tp()     { return _cfg_Tp();   }   // 脉冲宽度(s)
static inline double B()      { return _cfg_B();    }   // 信号带宽(Hz)
static inline double fs()     { return 3.0 * B();   }   // 采样频率(Hz),=3B(过采样率3)
static inline double gama()   { return B() / Tp();  }   // 线性调频率(Hz/s),=B/Tp
static inline double prf()    { return _cfg_prf();  }   // 脉冲重复频率(Hz)
static inline double lambda() { return c / fc();    }   // 波长(m),=c/fc
static inline double Vr()     { return _cfg_Vr();   }   // 相对径向速度(m/s)
static inline double Rs()     { return _cfg_Rs();   }   // 场景中心斜距(m)
static inline double wr()     { return _cfg_wr();   }   // 场景距离向宽度(m)
static inline double prt()    { return 1.0 / prf(); }   // 脉冲重复间隔(s),=1/prf
static inline int    nan1()   { return _cfg_nan1(); }   // 方位向脉冲数
static inline double A_RJ()   { return _cfg_A_RJ(); }   // 干扰幅度增益(dB)
static inline double amp_j()  { return pow(10, A_RJ() / 20.0); } // dB转线性幅度倍数

/* --- 物理常量和不常变的参数:保持const,不从配置文件读取 --- */
const double x_R0 = 0;         // 雷达的初始X坐标(m),固定为0
const double y_R0 = 0;         // 雷达的初始Y坐标(m),固定为0
const double amp = 1.0;        // 目标的反射系数,固定为1.0
const int    point_num = 1;    // 目标点数,固定为1

static inline double z_R0()   { return _cfg_z_R0(); }   // 雷达初始高度(m)

/* --- 派生参数:由基础参数计算得到 --- */
static inline int nrn() {
    return (int)floor((Tp() * fs() + wr()) / 2.0) * 2;
}                                    // 距离向采样点数(快时间),=floor((Tp*fs+wr)/2)*2,取偶数
static inline double Tnrn()   { return 1.0 / fs(); }               // 距离向采样间隔(s),=1/fs
static inline double Tstart() { return 2.0 * Rs() / c - nrn() / 2.0 / fs(); } // 距离向采样起始时间(s)
static inline double Tend()   { return 2.0 * Rs() / c + (nrn() / 2.0 - 1.0) / fs(); } // 距离向采样结束时间(s)

/* --- 全局信号矩阵和向量 --- */
inline MatrixXcd Radar_Sig;    // 雷达信号矩阵(nrn x nan1),复数类型,存储回波或干扰信号
inline MatrixXcd F;            // 距离-多普勒矩阵(nrn x nan1),复数类型,信号处理结果
inline VectorXd  tnrn;         // 距离向快时间向量(nrn x 1),单位:秒
inline VectorXd  win;           // 频域滤波窗(nrn x 1),0/1矩阵,|fr|<=B/2时为1
inline VectorXd  fr;            // 距离向频率向量(nrn x 1),单位:Hz
inline VectorXd  f;             // 载频序列(nan1 x 1),每个脉冲的载频(跳频模式下各脉冲不同)
inline VectorXcd phi1;         // 随机相位序列(nan1 x 1),每个脉冲的随机相位因子

/************************************************ 参数部分 结束 *************************************************/

/* --- 初始化距离向时间向量和频率向量 --- */
inline void init_tnrn_and_fr(VectorXd& tnrn, VectorXd& fr) {
    int _nrn = nrn();
    double _fs = fs();
    double _Tstart = Tstart();
    double _Tnrn = Tnrn();
    tnrn.resize(_nrn);
    fr.resize(_nrn);
    for (int i = 0; i < _nrn; ++i) {
        tnrn(i) = _Tstart + i * _Tnrn;          // 快时间轴:从Tstart开始,步长Tnrn=1/fs
        fr(i) = (i - _nrn / 2.0) / (_nrn / _fs); // 频率轴:零频居中,范围约[-fs/2, fs/2]
    }
}

#endif   // PARAMETERS_H
