# 04 - 信号处理模块 (Signal Processing)

## 功能概述

集成干扰识别和6种信号处理算法。运行流程为：先加载含干扰数据执行干扰类型识别(J_type=0~10)，再对6种处理模式逐个加载匹配数据并执行脉压、多普勒处理、速度解模糊、干扰解耦等操作，输出距离-多普勒矩阵或分离信号。

**数据流设计：** 每个Case独立加载匹配的输入数据：
- Case1~5 从模块01加载对应波形（跳频序列f、随机相位phi1、等效载频freq0等）
- Case6 从模块02加载含干扰回波（无数据时用EchoGenerator4兜底生成）
- shibie() 从模块02加载含干扰数据（与Case6相同来源）

## 目录结构

```
04_signal_processing/
├── CMakeLists.txt              # 构建配置，输出可执行文件 signal_proc + libsignal_processing_core.a 静态库
├── include/
│   ├── parameters.h            # 系统参数(与01模块完全一致的副本)
│   ├── Module0.h               # 工具函数库(FFT/IFFT/滤波/窗函数/文件IO)
│   ├── Module2.5.h             # 干扰识别函数 shibie() / gr_detection()
│   ├── Module3.h               # 6种信号处理函数定义(Case1~6) + 高精度辅助函数
│   ├── Config.h                # 配置管理器单例
│   ├── JamTarDivi.h            # Tsallis交叉熵干扰-目标信号解耦(q=1.0, 适配3×过采样)
│   ├── TfrStft.h               # 短时傅里叶变换(STFT)统一实现
│   ├── EchoGenerator4.h        # Case6兜底信号生成器(模块02数据不可用时)
│   ├── JammingSimulator4.h     # 兜底干扰模拟器(ISDJ/ISRJ/ISCJ/NBJ/RDJ)
│   └── signal_processing_core.h # 共享库 API: run_recognition/run_processing_rd/run_processing_decouple
├── src/
│   ├── main.cpp                # 入口：加载含干扰数据 → shibie() → chuli()
│   ├── Module2.5.cpp           # shibie() 实现：自动选择脉冲并识别干扰类型
│   ├── Module3.cpp             # chuli() + load_case_data() 实现：逐Case加载匹配数据
│   ├── test_case6.cpp          # Case6独立ISR测试(已从主构建排除)
│   └── signal_processing_core.cpp # 共享库：三个入口函数封装(供 GUI 使用)
└── bindings/
    └── signal_processing_bind.cpp # pybind11 绑定（薄适配层，供 04_GUI_signal_processing 使用）
```

## 运行流程

```cpp
main() {
    Config::instance().load();       // 加载外部配置文件
    init_tnrn_and_fr(tnrn, fr);     // 初始化距离向时间/频率向量

    // ── 加载含干扰数据(用于干扰识别) ──
    // 优先从模块02加载，无数据则用EchoGenerator4兜底
    load_jammed_data_for_shibie();  // 或 generate_fallback_signal()

    shibie();    // 干扰识别(使用模块02的含干扰数据)
    chuli();     // 6种处理模式自动循环(每个Case独立加载数据)
    promptToExit();
}

chuli() {
    for mode = 1 to 6:
        load_case_data(mode)    // 按Case加载匹配数据
        switch(mode):
            Case1~5: 从模块01加载对应波形
            Case6:   从模块02加载含干扰回波(或兜底)
        执行对应处理函数
        保存结果
}
```

### Per-Case 数据加载详情

| Case | 信号矩阵来源 | 辅助序列 | 处理函数 |
|------|-------------|---------|---------|
| 1 | `01_waveform_Case1_信号矩阵_signal.dat` | `01_waveform_Case1_跳频序列_freq_hop.dat` → 全局f | chuli_Case1() |
| 2 | `01_waveform_Case2_信号矩阵_signal.dat` | `01_waveform_Case2_随机相位序列_random_phase.dat` → 全局phi1 | chuli_Case2() |
| 3 | `01_waveform_Case3_信号矩阵_signal.dat` | (无辅助序列，用包络对齐测速) | chuli_Case3() |
| 4 | `01_waveform_Case4_信号矩阵_signal.dat` | `01_waveform_Case4_等效载频序列_freq_seq.dat` → 全局f | chuli_Case4() |
| 5 | `01_waveform_Case5_信号矩阵_signal.dat` | 跳频序列f + 随机相位序列phi1 | chuli_Case5() |
| 6 | `02_jamming_CaseN_含干扰回波_jammed.dat` | (无辅助序列，JamTarDivi盲分离) | chuli_Case6() |

**Case6兜底机制：** 若模块02数据不可用，按优先级尝试EchoGenerator4生成5种干扰类型(NBJ→RDJ→ISDJ→ISRJ→ISCJ)，最后兜底为纯LFM目标回波。

## 系统参数

与01/02模块共享同一套 `parameters.h`，参数说明见 `01_waveform_generation.md`。

## 干扰识别参数 (`Module2.5.h`)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| STFT频点数 | `recognition.stft_num` | 256 | STFT频率分量数 |
| 汉明窗长度 | `recognition.hamming_len` | 63 | 汉明窗长度 |
| 二值化阈值系数 | `recognition.threshold_ratio` | 0.3 | `miu1 = mean * 此值` |
| 上边界周期性阈值 | `recognition.periodic_std1` | 25 | `std_J_Ts1 < 此值` → 有周期性 |
| 下边界周期性阈值 | `recognition.periodic_std2` | 6 | `std_J_Ts2 < 此值` → 有周期性 |
| 脉冲数分类阈值 | `recognition.pulse_count_threshold` | 10 | `<此值→密集假目标, >=→频谱弥散` |
| 带宽比分类阈值 | `recognition.bw_ratio_threshold` | 1.5 | `<此值→灵巧噪声, >=→窄带` |

### 识别类型映射

| J_type | 干扰类型 | 判别依据 |
|--------|----------|----------|
| 0 | 无干扰/未识别 | 无上升沿 |
| 1 | 常规梳状谱 | 多上升沿, 单子带单脉冲 |
| 2 | 规则梳状谱 | 多上升沿, 子带脉冲数标准差=0 |
| 3 | 随机梳状谱 | 多上升沿, 子带脉冲数标准差>0 |
| 4 | 频谱弥散(SMSP) | 单上升沿, 调频率匹配且脉冲数>=10 |
| 5 | 灵巧噪声 | 单上升沿, 带宽比<1.5 |
| 6 | 窄带干扰 | 单上升沿, 带宽比>=1.5 |
| 7 | 密集假目标 | 单上升沿, 调频率匹配且脉冲数<10 |
| 8 | 扫频干扰 | 单上升沿, 调频率不匹配 |
| 9 | 梳状谱(间歇) | 多上升沿+时域下降沿<=1, 或单上升沿+多脉冲 |
| 10 | 联合拖引 | 单上升沿, 单子带单脉冲+单幅度上升沿 |

### 识别算法流程

```
输入信号 → STFT(256点, 汉明窗63) → JamLocated(Otsu) → fftshift
→ 频域包络 F_TEMP → 二值化(阈值=均值×0.3)
→ 上升沿/下降沿检测
├── 多上升沿 → 时域分析 → 脉冲周期性分析 → 调频率估计 → 分类
└── 单上升沿 → 频带分析 → 周期性分析 → 调频率/带宽分析 → 分类
```

## 信号处理参数 (`Module3.h` / `JamTarDivi.h`)

| 参数 | config.json key | 默认值 | 适用Case | 说明 |
|------|----------------|--------|---------|------|
| Kaiser窗beta | `processing.kaiser_beta` | 8 | Case3, Case4 | Kaiser窗旁瓣抑制参数 |
| 频率跳变步进 | `processing.case4_delta_f` | `B()` (40 MHz) | Case4 | 兜底线性步进(优先从01模块加载freq0) |
| STFT频点数 | `processing.divi_stft_num` | 256 | Case6 | JamTarDivi的STFT频率分量数 |
| 汉明窗长度 | `processing.divi_hamming_len` | 63 | Case6 | JamTarDivi的窗长度 |
| Tsallis q | `processing.tsallis_q` | 1.0 | Case6 | Tsallis熵参数(q=1.0=Shannon熵，适配3×过采样) |
| 高阶谱阈值 | `processing.gaojiepu_threshold` | 5.0 | Case6 | `max(|targetsignal|) < 此值`则标记 |
| 噪声基底倍数 | `processing.noise_floor_multiplier` | 3.0 | Case6 | 自适应量化: min_val = 中位数 × 此值 |

**注意：** 04模块的JamTarDivi参数优先从 `processing.*` 读取，兼容03模块的 `detection_suppression.*` 路径。q=1.0(Shannon熵)与03模块的q=1.2不同，这是因为04模块的3×过采样导致时频图67%为空，需要自适应量化+信号区域直方图来保证分割精度。

## 6种处理模式详解

### Case 1: 跳频信号处理 (速度补偿+解模糊)

```
数据来源: 01_Case1(信号矩阵 + 跳频序列f)
处理流程:
  动态下变频 exp(-j2πf(k)·t) [高精度] → 频域滤波(带宽B)
  → 脉冲压缩 → 多普勒补偿 → 距离-多普勒耦合补偿 → 速度解模糊 → 多普勒聚焦
```

- **关键**: 使用从01模块加载的全局 `f` 向量(跳频序列)进行下变频和耦合补偿
- **速度解模糊**: `NUM = floor((-Vr + vmax/2) / vmax)`, `vmax = c/(2·prt·fc)`
- **多普勒聚焦**: 对每个距离门g, 每个速度单元k, 构建补偿向量 W 并相干积累
- **输出**: `04_processing_Case1_距离多普勒图_rdmap.dat` (nrn×nan1 距离-多普勒矩阵)

### Case 2: 固定载频信号处理 (随机相位补偿)

```
数据来源: 01_Case2(信号矩阵 + 随机相位序列phi1)
处理流程:
  下变频 exp(-j2πfc·t) [高精度] → 随机相位补偿(phi1共轭)
  → 距离向FFT → 脉冲压缩 → 多普勒补偿 → 方位向FFT
```

- **关键**: 使用从01模块加载的全局 `phi1` 向量(随机相位)的共轭进行补偿
- **距离标度**: `xi = [0:nrn-1]/fs * c/2`
- **速度标度**: `yi = [0:nan1-1] * prf*c/fc/2/nan1`
- **输出**: `04_processing_Case2_距离多普勒图_rdmap.dat`

### Case 3: 传统脉冲压缩+速度估计 (解模糊)

```
数据来源: 01_Case3(信号矩阵, 基带LFM无载波调制)
处理流程:
  Kaiser窗(β=8)加窗脉压 → 峰值检测估计速度(包络对齐法)
  → 速度解模糊 → 多普勒相位补偿 → 多普勒聚焦
```

- **特点**: 输入为基带信号(无载波调制)，不需要下变频
- **速度估计**: 包络对齐法 `v_est = (sum_前半rols - sum_后半rols) / (nan1/2) * c*prf/(nan1*fs)`
- **模糊次数**: `NUM = floor((v_est + v1_max/2) / v1_max)`
- **输出**: `04_processing_Case3_距离多普勒图_rdmap.dat`

### Case 4: 改进型脉冲压缩 (二次相位补偿+速度解模糊)

```
数据来源: 01_Case4(信号矩阵 + 等效载频序列freq0)
处理流程:
  Kaiser窗加窗脉压 → 二次相位补偿(距离徙动校正RCMC)
  → 峰值位置检测 → 速度解模糊 → 多普勒聚焦
```

- **载频序列**: 优先从01模块加载等效载频序列freq0(随机排列跳频)，兜底使用线性步进
- **RCMC**: `s1(:,m) = lx0(:,m) * exp(j2π·2·xi/c·freq(m))`
- **与Case3区别**: 增加了距离徙动校正步骤，载频使用随机排列而非线性
- **输出**: `04_processing_Case4_距离多普勒图_rdmap.dat`

### Case 5: 复合处理 (跳频+随机相位联合补偿)

```
数据来源: 01_Case5(信号矩阵 + 跳频序列f + 随机相位序列phi1)
处理流程:
  联合下变频(跳频exp(-j2πf(k)·t)) + 相位补偿(phi1共轭)
  → 频域滤波 → 脉冲压缩 → 多普勒补偿 → 距离-多普勒耦合补偿
  → 速度解模糊 → 多普勒聚焦
```

- **关键**: 同时使用 `f` (跳频) 和 `phi1` (随机相位) 进行联合补偿
- **本质**: Case1 + Case2 的组合处理
- **输出**: `04_processing_Case5_距离多普勒图_rdmap.dat`

### Case 6: 时频干扰解耦 (Tsallis交叉熵)

```
数据来源: 02_CaseN(含干扰回波, 优先Case4~10, 无数据时EchoGenerator4兜底)
处理流程:
  对每个脉冲:
    JamTarDivi(data) → {jammingsignal, targetsignal, gaojiepu_idx, threshold}
```

- **算法**: Tsallis交叉熵(q=1.0=Shannon熵)二值分割STFT时频图
- **自适应量化**: 使用中位数×3.0作为噪声基底，信号区域直方图排除噪声bin
- **JSR诊断**: 输出 `JSR = 20*log10(target_energy/jam_energy)` (典型值 ~+7 dB)
- **输出**:
  - `04_processing_Case6_分离干扰信号_jam_sig.dat` (nrn×nan1 分离出的干扰信号)
  - `04_processing_Case6_分离目标信号_target_sig.dat` (nrn×nan1 分离出的目标信号)
  - `04_processing_Case6_解耦标志_decouple_flag.dat` (nan1×1 每脉冲的高阶谱标志)

## 输出文件汇总

| 文件 | 维度 | 内容 |
|------|------|------|
| `04_processing_Case{1-5}_距离多普勒图_rdmap.dat` | nrn×nan1 | 距离-多普勒矩阵 (complex) |
| `04_processing_Case6_分离干扰信号_jam_sig.dat` | nrn×nan1 | 分离出的干扰信号 |
| `04_processing_Case6_分离目标信号_target_sig.dat` | nrn×nan1 | 分离出的目标信号 |
| `04_processing_Case6_解耦标志_decouple_flag.dat` | nan1×1 | 高阶谱标志 (1=高阶谱, 0=否) |

## 与其他模块的数据依赖

### 从模块01加载 (Case1~5)
- `01_waveform_Case{1-5}_信号矩阵_signal.dat` — 5个信号矩阵
- `01_waveform_Case1_跳频序列_freq_hop.dat` — Case1跳频序列
- `01_waveform_Case2_随机相位序列_random_phase.dat` — Case2随机相位
- `01_waveform_Case4_等效载频序列_freq_seq.dat` — Case4等效载频
- `01_waveform_Case5_跳频序列_freq_hop.dat` — Case5跳频序列
- `01_waveform_Case5_随机相位序列_random_phase.dat` — Case5随机相位

### 从模块02加载 (shibie + Case6)
- `02_jamming_Case{4-10}_含干扰回波_jammed.dat` — 含干扰回波(优先Case4~10)

### 运行顺序建议
```bash
./01_waveform_generation/waveform_gen   # 先生成波形
./02_jamming_generation/jamming_gen     # 再生成干扰
./04_signal_processing/signal_proc      # 最后处理(读取01+02输出)
```

## 配置文件示例

```json
{
    "processing": {
        "kaiser_beta": 8,
        "case4_delta_f": null,
        "divi_stft_num": 256,
        "divi_hamming_len": 63,
        "tsallis_q": 1.0,
        "gaojiepu_threshold": 5.0
    },
    "recognition": {
        "stft_num": 256,
        "hamming_len": 63,
        "threshold_ratio": 0.3,
        "periodic_std1": 25,
        "periodic_std2": 6,
        "pulse_count_threshold": 10,
        "bw_ratio_threshold": 1.5
    }
}
```

## 依赖库

与01模块相同: C++17, Eigen 3.4.0, FFTW 3.3.10, 自实现 mini JSON 解析器 (third_party/nlohmann/json.hpp)
