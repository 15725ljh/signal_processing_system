# 03_GUI_detection - 雷达干扰识别与抑制系统

> **注意**: 本文档描述的是 `03_GUI_detection/`（干扰识别与抑制 GUI）。项目中还有三个独立的 GUI：`01_GUI_waveform/`（波形生成）、`02_GUI_jamming/`（干扰生成）、`04_GUI_signal_processing/`（信号处理），其设计文档分别见 [WAVEFORM_GUI_DESIGN.md](WAVEFORM_GUI_DESIGN.md)、[JAMMING_GUI_DESIGN.md](JAMMING_GUI_DESIGN.md)、[SIGNAL_PROCESSING_GUI_DESIGN.md](SIGNAL_PROCESSING_GUI_DESIGN.md)。

## 功能概述

基于 PySide6 的雷达干扰识别与抑制可视化桌面应用。**仅封装模块03（干扰识别与抑制）**，通过 pybind11 直接调用模块03的 `libdetection_core.a` 静态库，支持5种干扰类型(ISDJ/ISRJ/ISCJ/NBJ/RDJ)的参数配置、干扰识别、干扰-目标解耦和8种可视化图表。支持 macOS 和 Windows 双平台。

**注意：** 本GUI拥有独立的参数体系（fc=35GHz Ka波段），与模块01/02/04完全不同。GUI不涉及模块01/02/04，仅提供模块03的交互界面。

## 技术架构

```
用户界面 (PySide6 — ui/main_window.py 等)
    ↕
内联调用层 (main_window.py 中的 _run_detection_cpp)
    ↕ pybind11
C++ 绑定层 (03_jamming_detection_suppression/bindings/detection_bind.cpp → detection_cpp.pyd/.so)
    ↕
共享静态库 (03_jamming_detection_suppression/src/detection_core.cpp → libdetection_core.a)
    ↕
工具层 (EchoGenerator.h, JammingSimulator.h, GrDetection.h, JamTarDivi.h, TfrStft.h, JamLocated.h)
```

**核心特点：**
- 不通过文件I/O传递数据，所有信号数据通过内存 numpy 数组直接传递
- C++ 绑定代码位于 `03_jamming_detection_suppression/bindings/`，GUI 目录不含 C++ 源码
- 静态库 `libdetection_core.a` 由模块03构建，GUI 的 pybind11 编译时链接
- 依赖 Eigen + FFTW3
- 配置注入：通过临时 JSON 文件传递参数，调用 `Config::instance().loadFromFile()`
- 支持 macOS (.app) 和 Windows (.exe) 双平台打包

## 目录结构

```
03_GUI_detection/
├── app.py                          # 程序入口（图标加载、AppUserModelID 设置、lib/ 路径注册）
├── requirements.txt                # Python 依赖
│
├── lib/                            # 构建产物 (.gitignore 已忽略)
│   ├── detection_cpp.pyd           # C++ 绑定 (Windows, pybind11)
│   ├── detection_cpp.cpython-*.so  # C++ 绑定 (macOS, pybind11)
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时 (Windows)
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── build.bat                   # Windows 一键构建脚本
│   ├── build_ascii.bat             # ASCII-only 构建脚本（避免编码问题）
│   ├── 雷达干扰识别系统.spec        # PyInstaller macOS 打包配置
│   └── 雷达干扰识别系统_win.spec   # PyInstaller Windows 打包配置
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口 (C++ 调用、多线程计算、Win32 任务栏图标)
│   ├── param_panel.py              # 参数配置面板 (系统参数+识别/分离/生成器参数)
│   ├── plot_panel.py               # 可视化面板 (8种图表, PNG/SVG/数据导出)
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
│   └── __init__.py
│
├── venv/                           # Python 虚拟环境
└── dist/                           # 构建输出
```

## 功能详解

### 参数配置面板 (param_panel.py)

分为系统参数、识别参数、分离参数、时频分析参数和干扰生成器参数五个区域：

**系统参数（所有干扰类型共享）：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| fc | 35e9 Hz | 载波频率 (Ka波段，不同于全局16GHz) |
| B | 80e6 Hz | 信号带宽 (不同于全局40MHz) |
| fs | 120e6 Hz | 采样频率 |
| R0 | 1000 m | 目标初始距离 (不同于全局10km) |
| prf | 5e3 Hz | 脉冲重复频率 |
| Tp | 12e-6 s | 脉冲宽度 |
| nrn | 2048 | 信号采样点数 |
| cpiNum | 100 | CPI 数量（测试脉冲数） |
| SNR | 25 dB | 信噪比 |
| JSR | 30 dB | 干信比 |
| noise_snr | 25 dB | 底噪 SNR |
| r_min_ratio | 0.7 | 干扰距离下限比例 |
| r_max_ratio | 1.7 | 干扰距离上限比例 |

**识别参数 (GrDetection)：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| gr_stft_num | 256 | STFT 频点数 |
| gr_hamming_len | 63 | 汉明窗长度 |

**分离参数 (JamTarDivi)：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| divi_stft_num | 256 | 分离 STFT 频点数 |
| divi_hamming_len | 31 | 分离汉明窗长度 |
| tsallis_q | 1.2 | Tsallis 熵参数 q |
| gaojiepu_threshold | 35 | 高阶谱判定阈值 |

**时频分析参数 (TfrStft)：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| scale_factor | 32768 | STFT 缩放因子 (= 2^15) |

**干扰生成器参数 (JammingSimulator)：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| iscj_sub_T | 1e-6 s | ISCJ 子脉冲宽度 |
| isdj_sub_Ts | 2e-6 s | ISDJ 重复周期 |
| isrj_sub_Ts | 4e-6 s | ISRJ 重复周期 |
| nbj_center_freq | 15e6 Hz | NBJ 频带中心偏移 |
| nbj_noise_std | 5.0 | NBJ 高斯噪声标准差 |
| nbj_filter_order | 8 | NBJ 滤波器指数阶 |

**派生参数（自动计算显示）：** Kr=B/Tp, lambda=c/fc, prt=1/prf

**干扰类型选择：** 5 种干扰类型复选框（默认全选），支持"全选"/"全不选"。

### 可视化面板 (plot_panel.py)

支持8种图表标签页：

| 标签 | 类名 | 说明 |
|------|------|------|
| 时域 | TimeDomainPlot | 单脉冲时域波形（含干扰回波/含噪目标/分离干扰/分离目标），实部/虚部/包络切换，十字准线 |
| 频域 | FreqDomainPlot | 单脉冲频谱幅度(dB)，十字准线 |
| STFT | STFTPlotWidget | C++ 计算的 STFT 时频图，Jet 色表，60dB 动态范围 |
| 干扰定位 | JamMaskPlotWidget | STFT 幅度 + Otsu 二值掩模叠加（红色半透明），显示干扰占比 |
| 分离对比 | SeparationPlot | 含干扰回波/分离干扰/分离目标三线叠加对比，十字准线 |
| 分离时频 | SeparatedSTFTPlot | scipy.signal.stft 计算的分离后时频图，50dB 动态范围 |
| 距离-多普勒 | RDMapPlotWidget | RD 热力图（去载波+去斜+fftshift，物理坐标 m/m/s，十字准线） |
| 检测统计 | DetectionStatsPlot | 柱状图（4种识别类型投票）+ JSR干扰抑制比 信息面板 |

**CPI 选择器：** 脉冲索引下拉框，实时切换当前脉冲的时域/频域/分离视图（30ms 防抖）。

**RD 图物理坐标映射：**
```python
# 载波去除（通带信号 → 基带）
carrier_removal = exp(-j * 2π * (fc/fs) * n)

# 去斜
ref = exp(j * π * gama * t²)

# 距离轴
xi = fftshift(fftfreq(nrn, 1/fs)) * c / (2*gama)   # 距离 (m)

# 速度轴
dv = fftshift(fftfreq(cpiNum, 1/prf)) * lambda / 2   # 速度 (m/s)
```

**导出功能：**
- PNG 图片导出 (1920px 宽, pyqtgraph ImageExporter)
- SVG 矢量图导出 (pyqtgraph SVGExporter)
- 数据导出: 含干扰回波、含噪目标、分离干扰、分离目标、STFT 时频图、干扰掩模、识别结果

### C++ 调用层 (main_window.py)

`_run_detection_cpp()` 函数直接调用 `detection_cpp.run_detection()`：
- 将扁平参数字典（`detection_suppression.*` 前缀）转换为 C++ 配置
- 通过临时 JSON 文件注入配置（`Config::instance().loadFromFile()`）
- 返回 numpy 数组（echo_signal, stft_matrix, jam_mask, jamming_signal, target_signal 等）

**多线程计算：** `ComputeThread(QThread)` 遍历选中的干扰类型，逐类型调用 C++ 后端。信号：log → 控制台, progress → 状态栏, finished → 绘图。

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

C++ 后端通过 pybind11 返回以下数据：

| 字段 | 类型 | 说明 |
|------|------|------|
| echo_signal | complex128 (cpiNum×nrn) | 含干扰回波（全部 CPI） |
| s_echo_noise | complex128 (nrn,) | 含噪目标信号（第1个CPI） |
| stft_matrix | complex128 (STFT_NUM×nrn) | STFT 时频矩阵 |
| jam_mask | float64 (STFT_NUM×nrn) | Otsu 二值掩模 |
| detection_types | int64 (cpiNum,) | 每个CPI的识别结果 |
| jamming_signal | complex128 (nrn,) | 分离干扰信号 |
| target_signal | complex128 (nrn,) | 分离目标信号 |
| dominant_type | int | 主导干扰类型 |
| correct_count | int | 正确识别投票数 |
| cpiNum | int | CPI 数量 |
| nrn | int | 采样点数 |
| jsr | float | 干扰抑制比 (dB) |
| elapsed | float | 计算耗时 (s) |
| log_output | str | C++ 后端日志输出 |

## 配置文件

GUI 按以下顺序搜索 `config.json`：
1. 环境变量 `SPS_CONFIG`
2. 向上遍历目录树（最多10级）
3. `~/.config/sps/config.json`

读取 `detection_suppression.*` 配置节。通过临时 JSON 文件注入 C++ 配置。

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
| 参数体系 | system.* | system.* + jamming.* | detection_suppression.* | system.* + processing.* + detection_suppression.* |
| 载波频率 | 16 GHz | 16 GHz | 35 GHz | 16 GHz |
| 图表数 | 8种 | 7种 | 8种 | 7种 |

共享组件：theme.py, console_panel.py, scientific_spinbox.py, 资源文件。

## 与C++模块的关系

```
03_GUI_detection/app.py
    → ui/main_window.py (_run_detection_cpp)
        → detection_cpp.pyd / .so (pybind11, 位于 03_GUI_detection/lib/ 目录)
            → 03_jamming_detection_suppression/bindings/detection_bind.cpp
                → libdetection_core.a (静态库, 来自模块03)
                    → C++ detection_core.cpp (检测+分离流水线封装)
                    → C++ EchoGenerator.h (回波生成器)
                    → C++ JammingSimulator.h (5种干扰模拟器)
                    → C++ GrDetection.h (干扰类型识别)
                    → C++ JamTarDivi.h (Tsallis交叉熵解耦, q=1.2)
                    → C++ TfrStft.h (STFT实现)
                    → C++ JamLocated.h (Otsu阈值分割)
```

GUI直接调用C++函数，生成结果通过内存numpy数组返回给Python进行可视化，不经过文件I/O。配置通过临时JSON文件注入。
