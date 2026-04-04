# 03 - 干扰识别与抑制模块 (Jamming Detection & Suppression)

## 功能概述

模拟雷达回波信号中的干扰场景，对5种干扰类型进行自动识别和干扰抑制效果评估。每个干扰类型生成100个CPI脉冲进行统计识别，计算干扰抑制比(JSR)。该模块是独立的仿真验证系统，拥有自己的参数体系（`detection_suppression.*`）和信号生成链路，与01/02/04模块完全独立。

## 目录结构

```
03_jamming_detection_suppression/
├── CMakeLists.txt              # 构建配置，输出可执行文件 jamming_det_sup + libdetection_core.a 静态库
├── include/
│   ├── EchoGenerator.h         # 回波信号生成器(含目标回波+干扰叠加+AWGN)
│   ├── JammingSimulator.h      # 5种干扰信号模拟器类(ISCJ/ISDJ/ISRJ/NBJ/RDJ)
│   ├── GrDetection.h           # 干扰类型识别(STFT+Otsu+边缘检测)
│   ├── JamLocated.h            # Otsu阈值法时频图干扰区域定位
│   ├── JamTarDivi.h            # Tsallis交叉熵干扰-目标信号解耦(q=2.0)
│   ├── TfrStft.h               # 短时傅里叶变换(STFT)实现
│   ├── CountDuplicateVectors.h # 向量重复计数与统计
│   └── SignalWriter.h          # 信号数据文件写入
├── src/
│   ├── main.cpp                # 入口：Config::instance().load() → 自动循环5种干扰类型测试
│   └── detection_core.cpp      # 共享库：检测+分离流水线封装(供 GUI 使用)
└── bindings/
    └── detection_bind.cpp      # pybind11 绑定（薄适配层，供 03_GUI_detection 使用）
```

## 独立参数体系

本模块使用独立的雷达参数，通过 `detection_suppression.*` key 路径从 `config.json` 读取，与01/02/04模块不同：

### 基本系统参数 (`EchoGenerator.h` / `main.cpp`)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| fc | `detection_suppression.fc` | 35 GHz | 载波频率(Ka波段，不同于全局16GHz) |
| B | `detection_suppression.B` | 80 MHz | 信号带宽(不同于全局40MHz) |
| fs | `detection_suppression.fs` | 120 MHz | 采样频率 |
| R0 | `detection_suppression.R0` | 1000 m | 目标初始距离(不同于全局10km) |
| prf | `detection_suppression.prf` | 5 kHz | 脉冲重复频率 |
| Tp | `detection_suppression.Tp` | 12 μs | 脉冲宽度 |
| nrn | `detection_suppression.nrn` | 2048 | 信号采样点数 |
| cpiNum | `detection_suppression.cpiNum` | 100 | CPI 数量(测试脉冲数) |
| SNR | `detection_suppression.SNR` | 25 dB | 信噪比(目标回波加噪) |
| JSR | `detection_suppression.JSR` | 30 dB | 干信比(干扰信号幅度) |
| noise_snr | `detection_suppression.noise_snr` | 25 dB | 混合信号底噪SNR |
| r_min_ratio | `detection_suppression.r_min_ratio` | 0.7 | 干扰距离下限比例 |
| r_max_ratio | `detection_suppression.r_max_ratio` | 1.7 | 干扰距离上限比例 |

### 干扰识别参数 (`GrDetection.h`)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| stft_num | `detection_suppression.gr_stft_num` | 256 | 识别STFT频点数 |
| hamming_len | `detection_suppression.gr_hamming_len` | 63 | 识别汉明窗长度 |

### 干扰目标分离参数 (`JamTarDivi.h`)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| stft_num | `detection_suppression.divi_stft_num` | 256 | 分离STFT频点数 |
| hamming_len | `detection_suppression.divi_hamming_len` | 31 | 分离汉明窗长度 |
| tsallis_q | `detection_suppression.tsallis_q` | 1.2 | Tsallis熵参数q(独立值，04模块为1.0) |
| gaojiepu_threshold | `detection_suppression.gaojiepu_threshold` | 35 | 高阶谱判定阈值 |

### 时频分析参数 (`TfrStft.h`)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| scale_factor | `detection_suppression.scale_factor` | 32768 | STFT缩放因子 (= 2^15) |

### 干扰生成器参数 (`JammingSimulator.h`)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| iscj_sub_T | `detection_suppression.iscj_sub_T` | 1 μs | ISCJ子脉冲宽度 |
| isdj_sub_Ts | `detection_suppression.isdj_sub_Ts` | 2 μs | ISDJ重复周期 |
| isrj_sub_Ts | `detection_suppression.isrj_sub_Ts` | 4 μs | ISRJ重复周期 |
| nbj_center_freq | `detection_suppression.nbj_center_freq` | 15 MHz | NBJ频带中心偏移 |
| nbj_noise_std | `detection_suppression.nbj_noise_std` | 5.0 | NBJ高斯噪声标准差 |
| nbj_filter_order | `detection_suppression.nbj_filter_order` | 8 | NBJ滤波器指数阶 |

## 处理流程

```
对每种干扰类型 (RealLabel 1~5):
    1. generateEcho(cpiNum, label)  → 生成100个CPI的混合回波信号
       ├── 生成纯净LFM目标回波 s_echo
       ├── addAwgn_final()     → 添加25dB噪声得到 original_s_echo_noise
       └── JammingSimulator    → 生成干扰信号(按label选择类型)
           └── 按 JSR=30dB 归一化后叠加 + 再次添加底噪
    2. grDetection(B, Tp, data) → 对每个CPI进行干扰识别
       ├── TfrStft::tfrstft() → 计算STFT (256点, 汉明窗63)
       ├── jamLocated()        → Otsu阈值分割定位干扰区域
       └── 频谱特征提取 → 根据上升沿数量和周期性分类
    3. countDuplicateVectors() → 统计100个CPI的识别结果，取众数
    4. jamTarDivi(data)        → 对第1个CPI进行干扰-目标解耦
    5. 计算 JSR               → 干扰抑制比
    6. writeConsolidatedSignalsToFile() → 保存4路信号到文件
```

## 干扰类型定义

| Label | 类型 | 生成函数 | 说明 |
|-------|------|----------|------|
| 1 | ISDJ | `generateISDJ()` | 间歇直接转发干扰: Ts=2μs, T=1μs |
| 2 | ISRJ | `generateISRJ()` | 间歇重复转发干扰: Ts=4μs, T=1μs, 3次切片重组 |
| 3 | ISCJ | `generateISCJ()` | 间歇循环转发干扰: T=1μs, 三角递增子脉冲 |
| 4 | NBJ | `generateNBJ()` | 窄带瞄频干扰: 随机带宽(B/16~B/4), 15MHz偏移 |
| 5 | RDJ | `generateRDJ()` | 距离欺骗干扰: 窗口[Tp/4, 3Tp/4], 随机频偏 |

## 识别类型映射

| 识别结果 J_type | 含义 | 对应真实标签 |
|-----------------|------|-------------|
| 0 | 无干扰/未识别 | - |
| 1 | ISDJ/ISRJ/ISCJ (统一归类) | 1, 2, 3 |
| 2 | RDJ | 5 |
| 3 | NBJ | 4 |

## 干扰解耦算法 (`JamTarDivi.h`)

基于Tsallis交叉熵的时频域信号分割：

```
输入信号 → STFT(256点, 汉明窗31) → 幅度量化(0-255) → 灰度直方图
→ Tsallis交叉熵(q=1.2)计算256个阈值 → 最小熵阈值分割
→ 生成干扰掩模(高灰度=干扰, 低灰度=目标) → 提取干扰STFT → 列求和/weight重构时域
→ 目标信号 = 原始信号 - 干扰信号
```

**注意**: 03模块的 Tsallis q=1.2，与04模块的 q=1.0 不同。03模块使用1×过采样(nr=2048匹配Tp·fs)，04模块使用3×过采样。

## 核心类: JammingSimulator

```cpp
class JammingSimulator {
    // 构造参数: fc, B, fs, prf, Tp, nan, R0, R1
    // 内部: 自管理FFTW plan(构造分配, 析构释放)
    // 随机频偏: deltaf0 = unifrnd(-fs/6, fs/6)
    
    MatrixXcd generateISCJ();  // 间歇循环转发: 三角递增子脉冲组合
    MatrixXcd generateISDJ();  // 间歇直接转发: 周期采样+频偏
    MatrixXcd generateISRJ();  // 间歇重复转发: 采样+切片重组+频偏
    MatrixXcd generateNBJ();   // 窄带瞄频: FFT+8阶Butterworth滤波+15MHz偏移
    MatrixXcd generateRDJ();   // 距离欺骗: 窗口截断+LFM+频偏
};
```

## 输出文件

| 文件 | 内容 |
|------|------|
| `03_detection_识别与抑制日志_log.txt` | 测试汇总日志(识别率/JSR干扰抑制比/耗时) |
| `03_detection_type{1-5}_分离信号_signals.txt` | 4路信号合并(原始回波/干扰分量/分离干扰/分离目标, 8列) |

## 依赖库

- C++17
- Eigen 3.4.0
- FFTW 3.3.10
- OpenMP (可选，用于并行加速)
- 自实现 mini JSON 解析器 (third_party/nlohmann/json.hpp)
