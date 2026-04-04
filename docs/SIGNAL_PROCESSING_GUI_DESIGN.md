# 04_GUI_signal_processing - 雷达信号处理系统

> **注意**: 本文档描述的是 `04_GUI_signal_processing/`（信号处理 GUI）。项目中还有三个独立的 GUI：`01_GUI_waveform/`（波形生成）、`02_GUI_jamming/`（干扰生成）、`03_GUI_detection/`（干扰识别与抑制），其设计文档分别见 [WAVEFORM_GUI_DESIGN.md](WAVEFORM_GUI_DESIGN.md)、[JAMMING_GUI_DESIGN.md](JAMMING_GUI_DESIGN.md)、[DETECTION_GUI_DESIGN.md](DETECTION_GUI_DESIGN.md)。

## 功能概述

基于 PySide6 的雷达信号处理可视化桌面应用。**封装模块04（信号处理）+ 模块01（波形生成）**，通过 pybind11 直接调用 `libsignal_processing_core.a` 和 `libwaveform_core.a` 两个静态库，支持6种处理模式(Case1~6)的参数配置、信号处理和7种可视化图表。支持 macOS 和 Windows 双平台。

**注意：** 本GUI是唯一链接两个静态库的 GUI — Case1~5 需要模块01生成波形，Case6（干扰解耦）使用模块03的参数体系。

## 技术架构

```
用户界面 (PySide6 — ui/main_window.py 等)
    ↕
内联调用层 (main_window.py 中的 _run_processing_rd / _run_processing_decouple)
    ↕ pybind11
C++ 绑定层 (04_signal_processing/bindings/signal_processing_bind.cpp → signal_processing_cpp.pyd/.so)
    ↕
共享静态库:
  ├─ libsignal_processing_core.a (04_signal_processing/src/signal_processing_core.cpp)
  │    → Module3.h (6种处理函数) + Module2.5.h (干扰识别) + JamTarDivi.h (解耦)
  └─ libwaveform_core.a (01_waveform_generation/src/waveform_core.cpp)
       → waveform_core.h (5种波形生成函数)
```

**核心特点：**
- 不通过文件I/O传递数据，所有信号数据通过内存 numpy 数组直接传递
- C++ 绑定代码位于 `04_signal_processing/bindings/`，GUI 目录不含 C++ 源码
- 两个静态库由模块04和模块01分别构建，GUI 的 pybind11 编译时链接
- 依赖 Eigen + FFTW3
- 配置注入：通过临时 JSON 文件传递参数，调用 `Config::instance().loadFromFile()`
- 三个 C++ 入口：`run_processing_rd()` (Case1~5)、`run_processing_decouple()` (Case6)
- 支持 macOS (.app) 和 Windows (.exe) 双平台打包

## 目录结构

```
04_GUI_signal_processing/
├── app.py                          # 程序入口（图标加载、AppUserModelID 设置、lib/ 路径注册）
├── requirements.txt                # Python 依赖
│
├── lib/                            # 构建产物 (.gitignore 已忽略)
│   ├── signal_processing_cpp.pyd   # C++ 绑定 (Windows, pybind11)
│   ├── signal_processing_cpp.cpython-*.so  # C++ 绑定 (macOS, pybind11)
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时 (Windows)
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── build.bat                   # Windows 一键构建脚本
│   ├── build_ascii.bat             # ASCII-only 构建脚本（避免编码问题）
│   ├── 雷达信号处理系统.spec         # PyInstaller macOS 打包配置
│   └── 雷达信号处理系统_win.spec    # PyInstaller Windows 打包配置
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口 (C++ 调用、多线程计算、Win32 任务栏图标)
│   ├── param_panel.py              # 参数配置面板 (系统参数+波形参数+解耦参数)
│   ├── plot_panel.py               # 可视化面板 (7种图表, PNG/SVG/数据导出)
│   ├── console_panel.py            # 控制台日志面板
│   ├── scientific_spinbox.py       # 科学计数法输入控件
│   └── theme.py                    # 主题管理 (亮色/暗色 Catppuccin Mocha)
│
├── assets/                         # 资源文件
│   ├── __init__.py
│   ├── app_icon.ico                # Windows 应用图标
│   ├── app_icon.png                # PNG 图标 (256x256)
│   ├── icon_b64.txt                # PNG 图标 base64 编码 (运行时加载)
│   ├── checkmark.svg               # 勾选图标
│   └── chevron-down.svg            # 下拉箭头图标
│
├── core/                           # Python 后端模块
│   ├── __init__.py
│   ├── config_manager.py           # 配置管理 (config.json 读写)
│   └── signal_utils.py             # 信号处理工具函数
│
├── tests/                          # 测试文件
│   ├── test_algorithm_correctness.py  # C++ 算法正确性验证
│   └── test_plot_data.py             # GUI 绘图数据变换验证
│
├── venv/                           # Python 虚拟环境
└── dist/                           # 构建输出
```

## 功能详解

### 参数配置面板 (param_panel.py)

分为系统参数、波形参数、解耦参数三个区域：

**系统参数（所有Case共享）：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| fc | 16e9 Hz | 载波频率 (Ku波段) |
| B | 40e6 Hz | 信号带宽 |
| prf | 10e3 Hz | 脉冲重复频率 |
| Tp | 12e-6 s | 脉冲宽度 |
| Vr | 50 m/s | 目标径向速度 |
| Rs | 10000 m | 场景中心斜距 |
| wr | 608 m | 距离向宽度 |
| A_RJ | 10 dB | 干扰幅度增益 |
| z_R0 | 2000 m | 雷达初始高度 |
| nan1 | 64 | 方位向脉冲数 |

**波形参数（Case1~5 各有独立面板）：**

| Case | 名称 | 参数 | 默认值 |
|------|------|------|--------|
| 1 | 跳频信号处理 | 跳频点数 N, 频率步进 delta_f | 10, 40e6 Hz |
| 3 | 传统脉冲压缩 | 标称 PRT, 信号幅度, 抖动范围 | 1000e-6 s, 1.0, 20 μs |
| 4 | 改进型脉冲压缩 | 频率步进, 载频分组数, 标称 PRT, 抖动范围 | 40e6 Hz, 16, 1000e-6 s, 20 μs |
| 5 | 复合处理 | 跳频点数 N, 频率步进 delta_f | 10, 40e6 Hz |

**解耦参数（Case6）：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| divi_stft_num | 256 | 分离 STFT 频点数 |
| divi_hamming_len | 31 | 分离汉明窗长度 |
| tsallis_q | 2.0 | Tsallis 熵参数 q |
| gaojiepu_threshold | 35 | 高阶谱判定阈值 |
| SNR | 25 dB | 信噪比 |
| JSR | 30 dB | 干信比 |
| noise_snr | 25 dB | 底噪 SNR |
| r_min_ratio | 0.7 | 干扰距离下限比例 |
| r_max_ratio | 1.7 | 干扰距离上限比例 |

**派生参数（自动计算显示）：** fs=3×B, gama=B/Tp, lam=c/fc, prt=1/prf, nrn, Tstart 等

**Case 选择：** 6 个 Case 复选框（默认 Case1 全选）。Case6 自动处理全部 5 种干扰类型（ISDJ/ISRJ/ISCJ/NBJ/RDJ），无需手动选择。

### 可视化面板 (plot_panel.py)

支持7种图表标签页：

| 标签 | 类名 | 说明 |
|------|------|------|
| 脉冲特性 | PulseCharacteristicPlot | 双图：每脉冲峰值频率(MHz) + 跨脉冲解卷绕相位(rad)，十字准线 |
| 频谱 | FrequencyPlot | 单脉冲 FFT 频谱幅度(dB)，十字准线 |
| 信号矩阵 | ImagePlotWidget | 热力图 (实部/虚部/幅度/相位 切换)，viridis/CET-D1A 色表 |
| 距离-多普勒 | RDMapPlotWidget | RD 热力图（C++ 返回的物理坐标 xi/dv，自定义 Jet 色表，60dB 动态范围，十字准线） |
| 解耦结果 | DecouplePlot | Case6 分离对比（含干扰回波/分离干扰/分离目标/三线叠加），十字准线 |
| 结果汇总 | ResultSummaryPlot | Case1~5 峰值功率柱状图 / Case6 正常/高阶谱/JSR干扰抑制比 柱状图 |
| STFT | STFTPlotWidget | scipy.signal.stft 时频分析（去载波后），50dB 动态范围 |

**RD 图坐标（C++ 直接返回物理坐标）：**
```python
# C++ 计算，Python 直接使用
xi = fftshift(fftfreq(nrn, 1/fs)) * c / (2*gama)   # 距离 (m)
dv = fftshift(fftfreq(nan1, 1/prf)) * lambda / 2    # 速度 (m/s)
```

**STFT 载波去除：**
```python
# 通带信号需要先去载波再做 STFT
if fc > 0 and fs > 0:
    n = arange(len(col))
    carrier_phase = 2π * (fc/fs) * n
    col_bb = col * exp(-1j * carrier_phase)
```

**导出功能：**
- PNG 图片导出 (1920px 宽, pyqtgraph ImageExporter)
- SVG 矢量图导出 (pyqtgraph SVGExporter)
- 数据导出: RD图、输入信号、分离干扰、分离目标、解耦标志、距离轴、速度轴

### C++ 调用层 (main_window.py)

**两个 C++ 入口函数：**

`run_processing_rd(case_num, config_dict)` — Case1~5 距离-多普勒处理：
- 输入：Case 编号 (1-5)，完整配置字典
- 输出：rd_map (nrn×nan1), xi (nrn,), dv (nan1,), input_signal, nrn, nan1, elapsed, log_output

`run_processing_decouple(jam_type, config_dict)` — Case6 干扰解耦：
- 输入：干扰类型 (1-5)，完整配置字典
- 输出：jam_signal, target_signal, input_signal, decouple_flag, jsr_dB, avg_threshold, gaojiepu_count, nrn, nan1, elapsed, log_output

**多线程计算：** `ComputeThread(QThread)` 遍历选中的 Case，逐 Case 调用对应 C++ 入口。信号：log → 控制台, result → 绘图更新, allFinished → 完成。

### Windows 任务栏图标 (main_window.py)

与 01_GUI_waveform 相同的三要素方案：
1. `SetCurrentProcessExplicitAppUserModelID` — 创建窗口前调用
2. `SetClassLongPtrW(GCLP_HICONSM/HICON)` — 类级别图标设置
3. `QTimer.singleShot(200, ...)` — showEvent 中延迟调用

### 主题管理 (theme.py)

支持亮色/暗色双主题切换（View菜单），暗色使用 Catppuccin Mocha 配色方案。

### 控制台面板 (console_panel.py)

实时显示C++后端的stdout输出，支持彩色日志（INFO/WARNING/ERROR/SUCCESS），最多保留5000行。

## 返回数据结构

### run_processing_rd 返回 (Case1~5)

| 字段 | 类型 | 说明 |
|------|------|------|
| rd_map | complex128 (nrn×nan1) | 距离-多普勒矩阵 |
| xi | float64 (nrn,) | 距离轴 (m) |
| dv | float64 (nan1,) | 速度轴 (m/s) |
| input_signal | complex128 (nrn×nan1) | 输入信号矩阵 |
| nrn | int | 距离向采样点数 |
| nan1 | int | 方位向脉冲数 |
| case_num | int | Case 编号 |
| elapsed | float | 计算耗时 (s) |
| log_output | str | C++ 后端日志输出 |

### run_processing_decouple 返回 (Case6)

| 字段 | 类型 | 说明 |
|------|------|------|
| jam_signal | complex128 (nrn×nan1) | 分离干扰信号 |
| target_signal | complex128 (nrn×nan1) | 分离目标信号 |
| input_signal | complex128 (nrn×nan1) | 混合输入信号 |
| decouple_flag | int64 (nan1,) | 解耦标志 (0=正常, 1=高阶谱) |
| jsr_dB | float | 干扰抑制比 (dB) |
| avg_threshold | float | 平均阈值 |
| gaojiepu_count | int | 高阶谱脉冲数 |
| nrn | int | 距离向采样点数 |
| nan1 | int | 方位向脉冲数 |
| elapsed | float | 计算耗时 (s) |
| log_output | str | C++ 后端日志输出 |

## 配置文件

GUI 按以下顺序搜索 `config.json`：
1. 环境变量 `SPS_CONFIG`
2. 向上遍历目录树（最多10级）
3. `~/.config/sps/config.json`

读取 `system.*`、`processing.*`、`waveform.*`、`detection_suppression.*` 配置节。通过临时 JSON 文件注入 C++ 配置。

## 依赖

- Python 3.13+
- PySide6
- pyqtgraph 0.14+
- numpy
- scipy
- pybind11 (编译时)
- PyInstaller (打包时)

## 与其他 GUI 的关系

四个 GUI 完全独立，各自封装不同的 C++ 模块：

| 特性 | 01_GUI_waveform | 02_GUI_jamming | 03_GUI_detection | 04_GUI_signal_processing |
|------|-------------|-------------|---------------|---------------------|
| 封装模块 | 模块01 | 模块02 | 模块03 | 模块04+01 |
| 模式数 | 5 (Case1~5) | 10 (Case1~10) | 5 (干扰类型) | 6 (Case1~6) |
| C++ 绑定 | waveform_cpp.pyd | jamming_cpp.pyd | detection_cpp.pyd | signal_processing_cpp.pyd |
| 静态库 | libwaveform_core.a | libjamming_core.a | libdetection_core.a | libsignal_processing_core.a + libwaveform_core.a |
| 参数体系 | system.* | system.* + jamming.* | detection_suppression.* | system.* + processing.* + waveform.* + detection_suppression.* |
| C++ 入口数 | 1 | 1 | 1 | 3 (rd/decouple/recognition) |
| 图表数 | 8种 | 7种 | 8种 | 7种 |

共享组件：theme.py, console_panel.py, scientific_spinbox.py, signal_utils.py, 资源文件。

## 与C++模块的关系

```
04_GUI_signal_processing/app.py
    → ui/main_window.py
        ├─ _run_processing_rd() (Case1~5)
        │   → signal_processing_cpp.run_processing_rd()
        │       → libsignal_processing_core.a
        │           → chuli_Case1~5() (Module3.h)
        │           → load_case_data() (读取模块01生成的波形)
        │       → libwaveform_core.a
        │           → generate_waveform() (Case1~5 波形生成)
        │
        └─ _run_processing_decouple() (Case6)
            → signal_processing_cpp.run_processing_decouple()
                → libsignal_processing_core.a
                    → JamTarDivi() (Tsallis交叉熵解耦, q=1.0)
                    → EchoGenerator4() (兜底信号生成)
                    → JammingSimulator4() (兜底干扰模拟)
```

**Case6 参数体系说明：** Case6 使用 `detection_suppression.*` 参数路径（与模块03相同），但 Tsallis q 默认值为 1.0（不同于模块03的 1.2）。这是因为模块04使用 3× 过采样，时频图 67% 为空，需要自适应量化保证分割精度。

GUI直接调用C++函数，生成结果通过内存numpy数组返回给Python进行可视化，不经过文件I/O。配置通过临时JSON文件注入。
