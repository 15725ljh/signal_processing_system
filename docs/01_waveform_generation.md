# 01 - 波形生成模块 (Waveform Generation)

## 功能概述

生成Ku波段雷达的多种抗干扰波形信号，支持5种波形模式自动循环生成。输出为距离向-方位向二维复数信号矩阵 `Radar_Sig(nrn x nan1)`。所有参数通过 `Config.h` 从外部 `config.json` 读取，未配置则使用代码内置默认值。

本模块同时构建 `libwaveform_core.a` 静态库，供 GUI 的 pybind11 绑定直接链接，避免代码重复。

## 目录结构

```
01_waveform_generation/
├── CMakeLists.txt              # 构建配置，输出 waveform_gen 可执行文件 + libwaveform_core.a 静态库
├── include/
│   ├── parameters.h            # 全局参数定义(static inline函数 + CFG读取)
│   ├── Module0.h               # 工具函数库(FFT/IFFT/滤波/窗函数/文件IO/bessel_i0)
│   ├── Module1.h               # 5种波形生成函数定义(CFG读取waveform.*参数)
│   ├── waveform_core.h         # 共享库 API: generate_waveform() 入口
│   ├── waveform_params.h       # 共享库参数结构体 WaveformParams
│   └── Config.h                # 配置管理器单例(每个模块各有一份)
├── src/
│   ├── main.cpp                # 入口：Config::instance().load() → boxing() → promptToExit()
│   ├── Module1.cpp             # boxing() 实现：初始化参数并循环调用各模式
│   └── waveform_core.cpp       # 共享库：5个 Case 函数 + generate_waveform() 调度器
└── bindings/
    └── waveform_bind.cpp       # pybind11 绑定（薄适配层，~126行，供 GUI 使用）
```

### 静态库架构

```
waveform_core.cpp → libwaveform_core.a (仅依赖 Eigen)
                       ↕
           ┌───────────┴───────────┐
    waveform_gen (可执行文件)    waveform_cpp.so (GUI pybind11)
    额外链接: fftw3              额外链接: 无
```

- `libwaveform_core.a` 包含5个波形生成函数 + 派生参数计算，仅依赖 Eigen
- `waveform_gen` 可执行文件额外链接 fftw3（Module0.h 的 FFT/滤波功能）
- GUI 的 `waveform_cpp.so` 链接静态库，不依赖 fftw3

## 系统参数 (`parameters.h`)

### 雷达系统参数（从 `config.json` 的 `system.*` 路径读取）

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| `fc()` | `system.fc` | 16 GHz | Ku波段载波频率 |
| `Tp()` | `system.Tp` | 12 μs | 脉冲宽度 |
| `B()` | `system.B` | 40 MHz | 信号带宽 |
| `fs()` | 计算: `3*B()` | 120 MHz | 采样频率 (过采样率3) |
| `gama()` | 计算: `B()/Tp()` | 3.333×10¹² Hz/s | 线性调频率 |
| `prf()` | `system.prf` | 10 kHz | 脉冲重复频率 |
| `lambda()` | 计算: `c/fc()` | 0.01875 m | 波长 |
| `Vr()` | `system.Vr` | 50 m/s | 雷达-目标相对径向速度 |
| `Rs()` | `system.Rs` | 10000 m | 场景中心斜距 |
| `wr()` | `system.wr` | 608 m | 场景距离向宽度 |
| `prt()` | 计算: `1/prf()` | 100 μs | 脉冲重复间隔 |
| `A_RJ()` | `system.A_RJ` | 10 | 干扰幅度增益 (dB) |
| `amp_j()` | 计算: `10^(A_RJ/20)` | ~3.16 | dB转线性幅度倍数 |
| `z_R0()` | `system.z_R0` | 2000 m | 雷达初始高度 |
| `nan1()` | `system.nan1` | 64 | 方位向脉冲数 |

### 计算参数（由基础参数推导）

| 参数 | 值(默认参数下) | 说明 |
|------|----------------|------|
| `nrn()` | 2048 | `floor((Tp*fs+wr)/2)*2`，距离向采样点数(取偶数) |
| `Tnrn()` | 8.333 ns | `1/fs`，距离向采样间隔 |
| `Tstart()` | 66.663 μs | `2*Rs/c - nrn/(2*fs)`，距离向采样起始时间 |
| `Tend()` | 83.330 μs | `2*Rs/c + (nrn/2-1)/fs`，距离向采样结束时间 |

### 物理常量（固定值，不从配置读取）

| 变量 | 值 | 说明 |
|------|------|------|
| `c` | 3×10⁸ m/s | 光速 |
| `x_R0` | 0 m | 雷达初始X坐标 |
| `y_R0` | 0 m | 雷达初始Y坐标 |
| `amp` | 1.0 | 目标反射系数 |
| `point_num` | 1 | 目标点数 |

### 全局变量

| 变量 | 维度 | 类型 | 说明 |
|------|------|------|------|
| `Radar_Sig` | nrn × nan1 | MatrixXcd | 波形信号矩阵 |
| `F` | nrn × nan1 | MatrixXcd | 距离-多普勒矩阵 |
| `tnrn` | nrn × 1 | VectorXd | 距离向时间向量 |
| `fr` | nrn × 1 | VectorXd | 频率向量 (-fs/2 到 fs/2) |
| `win` | nrn × 1 | VectorXd | 频域滤波窗 |
| `f` | nan1 × 1 | VectorXd | 跳频载频序列 (Case1/5使用) |
| `phi1` | nan1 × 1 | VectorXcd | 随机复相位序列 (Case2/5使用) |

## 波形模式详解

### Case 1: 固定跳频波形

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| N | `waveform.case1_freq_hop.N` | 10 | 跳频点数 |
| delta_f | `waveform.case1_freq_hop.delta_f` | `B()` (40 MHz) | 频率步进 |

- **原理**: 每个脉冲从N个频点中随机选择一个，频点间隔 `delta_f`
- **信号模型**: `sig = win * exp(jπγ(t-2R/c)²) * exp(-j4πf(k)R/c + j2πf(k)t)`
- **使用全局变量**: `f` (跳频序列)
- **输出文件**: `01_waveform_Case1_信号矩阵_signal.dat`, `01_waveform_Case1_跳频序列_freq_hop.dat`

### Case 2: 随机相位波形

- 无额外配置参数
- **原理**: 固定载频 fc，每个脉冲附加 [0, 2π] 均匀分布的随机相位
- **信号模型**: `sig = win * exp(jπγ(t-2R/c)²) * exp(-j4πfcR/c + j2πfct) * phi1(i)`
- **使用全局变量**: `phi1` (随机相位序列)
- **输出文件**: `01_waveform_Case2_信号矩阵_signal.dat`, `01_waveform_Case2_随机相位序列_random_phase.dat`

### Case 3: 脉冲重复间隔抖动波形

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| prt | `waveform.case3_pri_jitter.prt` | 1000 μs | 标称脉冲重复间隔 |
| amp | `waveform.case3_pri_jitter.amp` | 1 | 信号幅度 |
| jitter_us | `waveform.case3_pri_jitter.jitter_us` | 20 | 抖动范围 (±μs) |

- **原理**: 标称 PRT=1000μs，加入 ±20μs 均匀随机抖动
- **信号模型**: `sig = win * amp * exp(jπγ(t-2R/c)²) * exp(-j4πfcR/c)`
- **输出文件**: `01_waveform_Case3_信号矩阵_signal.dat`, `01_waveform_Case3_等效载频序列_freq_seq.dat`

### Case 4: 混合波形 (跳频 + 脉冲抖动)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| delta_f | `waveform.case4_hybrid.delta_f` | `B()` (40 MHz) | 频率步进 |
| fcnum | `waveform.case4_hybrid.fcnum` | 16 | 载频分组数 |
| prt | `waveform.case4_hybrid.prt` | 1000 μs | 标称脉冲重复间隔 |
| amp | `waveform.case4_hybrid.amp` | 1 | 信号幅度 |
| jitter_us | `waveform.case4_hybrid.jitter_us` | 20 | 抖动范围 (±μs) |

- **原理**: 16个载频分为一组，组内随机排列；同时叠加 ±20μs PRT抖动
- **信号模型**: 同 Case3 但使用跳频后的载频 `freq(k)`
- **输出文件**: `01_waveform_Case4_信号矩阵_signal.dat`, `01_waveform_Case4_等效载频序列_freq_seq.dat`

### Case 5: 跳频 + 随机相位复合波形

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| N | `waveform.case5_combined.N` | 10 | 跳频点数 |
| delta_f | `waveform.case5_combined.delta_f` | `B()` (40 MHz) | 频率步进 |

- **原理**: Case1 + Case2 的组合，同时使用跳频和随机相位
- **信号模型**: `sig = win * exp(jπγ(t-2R/c)²) * exp(-j4πf(k)R/c + j2πf(k)t) * phi1(k)`
- **使用全局变量**: `f` (跳频序列), `phi1` (随机相位序列)
- **输出文件**: `01_waveform_Case5_信号矩阵_signal.dat`, `01_waveform_Case5_跳频序列_freq_hop.dat`, `01_waveform_Case5_随机相位序列_random_phase.dat`

## 配置文件示例

```json
{
    "system": {
        "fc": 16e9,
        "Tp": 12e-6,
        "B": 40e6,
        "prf": 10e3,
        "Vr": 50,
        "Rs": 10000,
        "wr": 608,
        "nan1": 64
    },
    "waveform": {
        "case1_freq_hop": { "N": 10 },
        "case3_pri_jitter": { "prt": 1000e-6, "amp": 1, "jitter_us": 20 },
        "case4_hybrid": { "delta_f": null, "fcnum": 16, "prt": 1000e-6, "amp": 1, "jitter_us": 20 },
        "case5_combined": { "N": 10, "delta_f": null }
    }
}
```

## 工具函数 (`Module0.h`)

### 信号处理

| 函数 | 签名 | 说明 |
|------|------|------|
| `fft` | `VectorXcd -> VectorXcd` | FFTW正向FFT |
| `ifft` | `VectorXcd -> VectorXcd` | FFTW逆向FFT (含1/N归一化) |
| `fftshift` | 向量/矩阵版本 | 零频移到中心 |
| `filter` | `(b,a,x) -> y` | IIR滤波器 (等价MATLAB filter) |
| `butter` | `(order,cutoff) -> (b,a)` | Butterworth低通滤波器设计 |
| `tfrstft` | `(x,t,N,h) -> (tfr,t_out,f)` | 短时傅里叶变换 |

### 窗函数

| 函数 | 签名 | 说明 |
|------|------|------|
| `kaiser` | `(N, beta) -> VectorXd` | Kaiser窗 (使用自实现 `bessel_i0()`) |
| `generateHammingWindow` | `(M) -> VectorXd` | 归一化汉明窗 |

### 干扰分析

| 函数 | 说明 |
|------|------|
| `JamTarDivi` | 基于Tsallis交叉熵的干扰-目标信号解耦 (q=1.5) |
| `JamLocated` | 基于Otsu阈值法的时频图干扰区域定位 |
| `awgn` | 添加指定SNR的高斯白噪声 |

### 文件IO

| 函数 | 说明 |
|------|------|
| `saveVector` | 实/复向量保存为 .dat |
| `saveMatrix` | 实/复矩阵保存为 .dat (首行行列数) |

## 自实现函数

### `bessel_i0(double x)` — 修正贝塞尔函数 I₀

替代 Boost `cyl_bessel_i(0, x)`。使用 Taylor 级数展开：

```
I₀(x) = Σ_{k=0}^{∞} (x²/4)^k / (k!)²
```

- 收敛阈值: `term < 1e-15 * sum`
- 最大迭代: 60
- 精度: 16个测试点 (x=0~40) 相对误差均 < 5e-16 (机器 epsilon 级别)

## 依赖库

- C++17
- Eigen 3.4.0 (矩阵运算)
- FFTW 3.3.10 (FFT/IFFT) — 可执行文件使用，静态库不依赖
- 自实现 mini JSON 解析器 (配置文件解析，位于 third_party/nlohmann/json.hpp)
