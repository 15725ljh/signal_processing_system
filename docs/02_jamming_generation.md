# 02 - 干扰生成模块 (Jamming Generation)

## 功能概述

在01模块生成的波形信号基础上叠加10种不同类型的雷达干扰信号，模拟敌方电子对抗环境。运行时自动循环模式1-10，前一个模式的输出作为下一个模式的输入（级联叠加）。所有参数通过 `Config.h` 从外部 `config.json` 读取。

## 目录结构

```
02_jamming_generation/
├── CMakeLists.txt              # 构建配置，输出可执行文件 jamming_gen
├── include/
│   ├── parameters.h            # 系统参数(与01模块完全一致的副本)
│   ├── Module0.h               # 工具函数库(与01模块完全一致的副本)
│   └── Module2.h               # 10种干扰生成函数定义(CFG读取jamming.*参数)
└── src/
    ├── main.cpp                # 入口：Config::instance().load() → ganrao() → promptToExit()
    └── Module2.cpp             # ganrao() 实现：循环调用各模式，级联叠加
```

## 输入/输出

- **输入**: `Radar_Sig(nrn × nan1)` — 初始化为全零矩阵，**不从模块01加载**
- **干扰分类**:
  - **转发型** (Case1~3): 内部生成目标回波作为转发源，不依赖外部信号输入
  - **生成型** (Case4~10): 自行生成干扰信号后叠加到 `Radar_Sig` 上
- **级联叠加**: 每个Case的输出成为下一个Case的 `Radar_Sig` 输入
- **输出**: 每个模式生成两个文件：
  - `02_jamming_Case{N}_含干扰回波_jammed.dat` — 含干扰的总回波信号 (nrn × nan1)
  - `02_jamming_Case{N}_干扰类型缩写.dat` — 单独的干扰信号 (nrn × nan1)

## 内部目标回波生成 (转发型干扰 Case1~3)

Case1~3 为转发型干扰，需要截获真实雷达信号作为转发源。由于模块02不从模块01加载数据（`Radar_Sig` 初始化为全零），这三个Case在内部自行生成目标回波信号：

```
目标回波模型:
  s(t,k) = rect((t - 2Rt/c) / Tp) × exp(jπγ(t - 2Rt/c)²) × exp(-j4πRt/λ)

其中:
  Rt = Rs - Vr·k/PRF         (目标距离随慢时变化)
  rect(x) = 1 当 |x| ≤ 0.5   (矩形窗截取脉冲宽度内信号)
  距离相位使用 precise_expj_2pi_scalar() 保证大数值精度
```

该目标回波作为"截获信号"送入干扰生成流程（FFT → 频域相位调制 → IFFT），产生的干扰信号再与目标回波叠加形成最终输出。

## 级联机制

```cpp
// Module2.cpp 核心逻辑
for (int mode = 1; mode <= 10; ++mode) {
    ganrao_CaseN(Radar_Sig, echo_target, Jam_Sig);  // 在Radar_Sig上叠加干扰
    Radar_Sig = echo_target;  // 输出成为下一个模式的输入
}
```

## 系统参数

与01模块共享同一套 `parameters.h`，参数说明见 `01_waveform_generation.md`。

## 10种干扰模式详解

### Case 1: 距离假目标干扰 (RDJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| jj | `jamming.case1_rdj.jj` | 1 | 干扰脉冲延迟数 |
| Rj | `jamming.case1_rdj.Rj` | 100 m | 假目标距离 |
| t0 | 计算: `2*Rj/c` | 666.7 ns | 时延 |
| amp_j | `jamming.case1_rdj.amp_j` | 10 | 干扰幅度增益(线性倍数，非dB) |

**原理**: 内部生成目标回波 → 截获前向脉冲回波 → FFT → 频域乘时延相位 `exp(-j2πf·Δt)` → IFFT，生成距离维虚假目标。前jj个脉冲保持目标回波不变。

### Case 2: 速度假目标干扰 (VDJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| Vj | `jamming.case2_vdj.Vj` | 100 km/s | 假目标速度 |
| jj | `jamming.case2_vdj.jj` | 1 | 干扰延迟脉冲数 |
| Rj | `jamming.case2_vdj.Rj` | 10 m | 假目标距离 |
| amp_j | `jamming.case2_vdj.amp_j` | 10 | 干扰幅度增益(线性倍数) |
| fj | 计算: `2*Vj/lambda()` | 10.667 THz | 假目标多普勒频率 |

**原理**: 内部生成目标回波 → 截获前向脉冲 → 施加多普勒调制 `exp(j2πfj·t)` → FFT → 时延相位 → IFFT，在速度维产生虚假目标。前jj个脉冲保持目标回波不变。

### Case 3: 间歇采样转发干扰 (ISRJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| Ts_ISRJ | `jamming.case3_isrj.Ts_ISRJ` | 4 μs | 采样周期 |
| T_ISRJ | `jamming.case3_isrj.T_ISRJ` | `Ts_ISRJ/4` (1 μs) | 采样脉宽 |
| Rj | `jamming.case3_isrj.Rj` | 10 m | 干扰目标距离 |
| amp_j | `jamming.case3_isrj.amp_j` | 10 | 干扰幅度增益(线性倍数) |
| Ts_Num | 计算: `Ts_ISRJ*fs()` | 480 | 采样脉宽采样点数 |
| T_Num | 计算: `T_ISRJ*fs()` | 120 | 采样周期采样点数 |
| N_ISRJ | 计算: `Tp/Ts_ISRJ` | 3 | 采样周期数 |
| Num_C_I | 计算: `Ts_ISRJ/T_ISRJ-1` | 3 | 切片重组次数 |

**原理**: 内部生成目标回波 → 检测有效区域 → 周期性采样窗口截取片段 → 多普勒切片重组（FFT + 频移 + IFFT）→ 距离时延相位，在距离-多普勒二维平面产生多个虚假目标（"弹幕"式干扰）。

### Case 4: 窄带噪声干扰 (NNJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| power_dBW | `jamming.case4_nnj.power_dBW` | 20 | 噪声功率 (dBW) |
| butter_order | `jamming.case4_nnj.butter_order` | 8 | 低通滤波器阶数 |
| butter_cutoff | `jamming.case4_nnj.butter_cutoff` | 0.3 | 归一化截止频率 |

- **power_linear**: 计算: `10^(power_dBW/10)` = 100
- **sigma**: 计算: `sqrt(power_linear/2)` = 7.071

**原理**: 生成复高斯白噪声 → 载波调制 → 低通滤波 → 抑制高频分量 → IFFT，产生带限遮盖式干扰。

### Case 5: 距离波门拖引干扰 (RGPOJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| Vj | `jamming.case5_rgpo.Vj` | 340 m/s | 拖引速度 |
| amp_target | `jamming.case5_rgpo.amp_target` | 1.0 | 目标幅度 |
| amp_jammer | `jamming.case5_rgpo.amp_jammer` | 1.4 | 干扰幅度 |
| drag_stages | `jamming.case5_rgpo.drag_stages` | 4 | 拖引阶段数 |
| awgn_snr | `jamming.case5_rgpo.awgn_snr` | 10 dB | AWGN信噪比 |

**三阶段策略**:
1. **捕获** (k=0): `Rj = Rt - Vj·(k+0.01)/prf·4000` 建立初始偏移
2. **拖引** (k<drag_stages): `Rj = Rt - Vj·k/prf·4000` 匀速拖离
3. **反转** (k>=drag_stages): `Rj = Rt + Vj·(k-drag_stages+1)/prf·4000` 破坏跟踪

### Case 6: 速度波门拖引干扰 (VGPOJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| Vj | `jamming.case6_vgpo.Vj` | 10 km/s | 假目标速度 |
| amp_target | `jamming.case6_vgpo.amp_target` | 1.0 | 目标幅度 |
| amp_jammer | `jamming.case6_vgpo.amp_jammer` | 1.4 | 干扰幅度 |
| drag_stages | `jamming.case6_vgpo.drag_stages` | 4 | 拖引阶段数 |
| awgn_snr | `jamming.case6_vgpo.awgn_snr` | 10 dB | AWGN信噪比 |
| fd | 计算: `2*Vr()/lambda()` | 5333 Hz | 目标多普勒频率 |

**原理**: 在速度维制造渐进变化的虚假运动，通过多普勒频移 `fj = 2Vj/λ·k/prf·40000` 诱使雷达速度跟踪门偏离。

### Case 7: 密集复制转发假目标干扰 (DRFTJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| JSR | `jamming.case7_drftj.JSR` | 0 dB | 干信比 |
| amp_target | `jamming.case7_drftj.amp_target` | 1.0 | 目标幅度 |
| num_jam | `jamming.case7_drftj.num_jam` | 50 | 转发次数 |
| detaR | `jamming.case7_drftj.detaR` | 50 m | 每次转发距离增量 |
| awgn_snr | `jamming.case7_drftj.awgn_snr` | 10 dB | AWGN信噪比 |
| amp_jammer | 计算: `amp_target*10^(JSR/20)` | 1.0 | 干扰幅度 |

**原理**: 在真实目标附近产生50个等间隔假目标（前3次正向偏移，后续反向偏移），形成动态欺骗效果。

### Case 8: 脉内前沿切片重复假目标干扰 (IPLESRJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| A_RJ | `jamming.case8_iplesrj.A_RJ` | 30 dB | 干扰机发射功率增益 |
| amp_target | `jamming.case8_iplesrj.amp_target` | 1.0 | 目标幅度 |
| R0_new | `jamming.case8_iplesrj.R0_new` | 608 m | 专用初始距离(独立于全局Rs) |
| V_ISRJ | `jamming.case8_iplesrj.V_ISRJ` | 0 m/s | 雷达-干扰机径向速度 |
| T_ISRJ_ratio | `jamming.case8_iplesrj.T_ISRJ_ratio` | 16 | 干扰间隔 = Tp/此值 |
| R_ahead | `jamming.case8_iplesrj.R_ahead` | 0 s | 干扰机提前发射时间 |
| awgn_snr | `jamming.case8_iplesrj.awgn_snr` | 10 dB | AWGN信噪比 |
| T_ISRJ | 计算: `Tp/T_ISRJ_ratio` | 0.75 μs | 干扰间隔时间 |
| N_ISRJ | 计算: `Tp/T_ISRJ` | 16 | 转发次数 |

**注意**: 此模式使用独立的距离参数体系 (R0_new=608m)，与全局 Rs=10000m 不同。使用独立的 `nrn_new` 和 `tnrn_new` 计算距离向采样参数。

**原理**: 对脉内前沿切片进行16次重复转发，在距离维产生等间隔假目标群。使用双时域窗 (切片窗 + 脉宽保护窗) 防止溢出。

### Case 9: 频谱弥散干扰 (SMSPJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| R0 | `jamming.case9_smsp.R0` | 10000 m | 目标初始距离 |
| num_slices | `jamming.case9_smsp.num_slices` | 4 | 子脉冲数 |
| JSR | `jamming.case9_smsp.JSR` | 15 dB | 干信比 |
| amp_target | `jamming.case9_smsp.amp_target` | 1.0 | 目标幅度 |
| amp_extra | `jamming.case9_smsp.amp_extra` | 1.4 | 干扰幅度额外系数 |
| awgn_snr | `jamming.case9_smsp.awgn_snr` | 10 dB | AWGN信噪比 |
| gama_j | 计算: `num_slices*gama()` | 4γ | 增强调频率 |
| Tp_j | 计算: `Tp/num_slices` | 3 μs | 子脉冲时宽 |

**原理**: 将脉冲分割为4个调频率增强的子脉冲，利用匹配滤波失配在距离像主瓣两侧生成虚假峰。

### Case 10: 梳状谱干扰 (COMBJ)

| 参数 | config.json key | 默认值 | 说明 |
|------|----------------|--------|------|
| R0 | `jamming.case10_comb.R0` | 10000 m | 目标初始距离 |
| num_tones | `jamming.case10_comb.num_tones` | 7 | 频谱线数 |
| JSR | `jamming.case10_comb.JSR` | 0 dB | 干信比 |
| amp_target | `jamming.case10_comb.amp_target` | 1.0 | 目标幅度 |
| deltaf | `jamming.case10_comb.deltaf` | 1 MHz | 频率间隔 |
| awgn_snr | `jamming.case10_comb.awgn_snr` | 10 dB | AWGN信噪比 |

**原理**: 在7个离散频率点生成干扰信号（正负偏移对称分布），在匹配滤波后形成等间隔假目标群。

## 配置文件示例

```json
{
    "jamming": {
        "case1_rdj": { "jj": 1, "Rj": 100, "amp_j": 10 },
        "case2_vdj": { "Vj": 1e5, "jj": 1, "Rj": 10, "amp_j": 10 },
        "case3_isrj": { "Ts_ISRJ": 4e-6, "T_ISRJ": null, "Rj": 10, "amp_j": 10 },
        "case4_nnj": { "power_dBW": 20, "butter_order": 8, "butter_cutoff": 0.3 },
        "case5_rgpo": { "Vj": 340, "amp_target": 1.0, "amp_jammer": 1.4, "drag_stages": 4, "awgn_snr": 10 },
        "case6_vgpo": { "Vj": 1e4, "amp_target": 1.0, "amp_jammer": 1.4, "drag_stages": 4, "awgn_snr": 10 },
        "case7_drftj": { "JSR": 0, "amp_target": 1.0, "num_jam": 50, "detaR": 50, "awgn_snr": 10 },
        "case8_iplesrj": { "A_RJ": 30, "amp_target": 1.0, "R0_new": 608, "V_ISRJ": 0, "T_ISRJ_ratio": null, "R_ahead": 0, "awgn_snr": 10 },
        "case9_smsp": { "R0": 10000, "num_slices": 4, "JSR": 15, "amp_target": 1.0, "amp_extra": 1.4, "awgn_snr": 10 },
        "case10_comb": { "R0": 10000, "num_tones": 7, "JSR": 0, "amp_target": 1.0, "deltaf": 1e6, "awgn_snr": 10 }
    }
}
```

## 依赖库

与01模块相同: Eigen 3.4.0, FFTW 3.3.10, 自实现 mini JSON 解析器 (third_party/nlohmann/json.hpp)
