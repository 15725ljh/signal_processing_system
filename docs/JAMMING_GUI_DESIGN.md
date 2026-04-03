# GUI_jamming - 雷达干扰生成系统

## 功能概述

基于 PySide6 的雷达干扰生成与可视化桌面应用。**仅封装模块02（干扰生成）**，通过 pybind11 直接调用模块02的 `libjamming_core.a` 静态库，支持10种干扰模式(Case1~10)的参数配置、干扰生成和7种可视化图表。支持 macOS 和 Windows 双平台。

**注意：** 本GUI不涉及模块01/03/04，仅提供模块02的交互界面。

## 技术架构

```
用户界面 (PySide6 — ui/main_window.py 等)
    ↕
内联调用层 (main_window.py 中的 _run_jamming_cpp)
    ↕ pybind11
C++ 绑定层 (02_jamming_generation/bindings/jamming_bind.cpp → jamming_cpp.pyd/.so)
    ↕
共享静态库 (02_jamming_generation/src/jamming_core.cpp → libjamming_core.a)
    ↕
工具层 (Module0.h, Module2.h, jamming_params.h)
```

**核心特点：**
- 不通过文件I/O传递数据，所有干扰数据通过内存 numpy 数组直接传递
- C++ 绑定代码位于 `02_jamming_generation/bindings/`，GUI 目录不含 C++ 源码
- 静态库 `libjamming_core.a` 由模块02构建，GUI 的 pybind11 编译时链接
- 依赖 Eigen + FFTW3（与 GUI_waveform 仅依赖 Eigen 不同）
- 支持 macOS (.app) 和 Windows (.exe) 双平台打包

## 目录结构

```
GUI_jamming/
├── app.py                          # 程序入口（图标加载、AppUserModelID 设置、lib/ 路径注册）
├── requirements.txt                # Python 依赖
│
├── lib/                            # 构建产物 (.gitignore 已忽略)
│   ├── jamming_cpp.pyd             # C++ 绑定 (Windows, pybind11)
│   ├── jamming_cpp.cpython-*.so    # C++ 绑定 (macOS, pybind11)
│   ├── libgcc_s_seh-1.dll          # MinGW 运行时 (Windows)
│   ├── libstdc++-6.dll
│   └── libwinpthread-1.dll
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # macOS 一键构建脚本
│   ├── build.bat                   # Windows 一键构建脚本
│   ├── setup_cython.py             # Cython 编译配置 (备用)
│   ├── 雷达干扰生成系统.spec         # PyInstaller macOS 打包配置
│   └── 雷达干扰生成系统_win.spec    # PyInstaller Windows 打包配置
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口 (C++ 调用、Win32 任务栏图标、配置管理)
│   ├── param_panel.py              # 参数配置面板 (系统参数+10种Case参数)
│   ├── plot_panel.py               # 增强版可视化面板 (7种图表)
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
├── venv/                           # Python 虚拟环境
└── dist/                           # 构建输出
```

## 功能详解

### 参数配置面板 (param_panel.py)

分为系统参数和Case参数两个区域：

**系统参数（10个Case共享）：**

| 参数 | 默认值 | 说明 |
|------|--------|------|
| fc | 16e9 Hz | 载波频率 (Ku波段) |
| Tp | 12e-6 s | 脉冲宽度 |
| B | 40e6 Hz | 信号带宽 |
| prf | 10e3 Hz | 脉冲重复频率 |
| Vr | 50 m/s | 目标径向速度 |
| Rs | 10000 m | 场景中心斜距 |
| wr | 608 m | 距离向宽度 |
| A_RJ | 10 dB | 干扰幅度增益 |
| z_R0 | 2000 m | 雷达初始高度 |
| nan1 | 64 | 方位向脉冲数 |

**Case参数（每种独立面板）：**

| Case | 名称 | 参数 |
|------|------|------|
| 1 | RDJ 距离假目标 | jj, Rj, amp_j |
| 2 | VDJ 速度假目标 | Vj, jj, Rj, amp_j |
| 3 | ISRJ 间歇采样 | Ts_ISRJ, T_ISRJ, Rj, amp_j |
| 4 | NNJ 窄带噪声 | power_dBW, butter_order, butter_cutoff |
| 5 | RGPO 距离拖引 | Vj, amp_target, amp_jammer, drag_stages, awgn_snr |
| 6 | VGPO 速度拖引 | Vj, amp_target, amp_jammer, drag_stages, awgn_snr |
| 7 | DRFTJ 密集假目标 | JSR, amp_target, num_jam, detaR, awgn_snr |
| 8 | IPLESRJ 前沿切片 | A_RJ, amp_target, R0_new, V_ISRJ, T_ISRJ_ratio, R_ahead, awgn_snr |
| 9 | SMSP 频谱弥散 | num_slices, JSR, amp_target, amp_extra, R0, awgn_snr |
| 10 | COMB 梳状谱 | num_tones, JSR, amp_target, deltaf, R0, awgn_snr |

**派生参数（自动计算显示）：** fs=3×B, gama=B/Tp, nrn, lambda, prt=1/prf 等

### 可视化面板 (plot_panel.py)

支持7种图表标签页：

| 标签 | 类名 | 说明 |
|------|------|------|
| 时域 | TimeDomainPlot | 目标/干扰/合成信号波形 (实部/虚部/包络)，信号类型切换 |
| 频域 | FreqDomainPlot | 频谱幅度(dB)，信号类型切换 |
| 信号矩阵 | ImagePlotWidget | 热力图 (echo_target/jam_signal/target_signal 切换) |
| 距离-多普勒 | RangeDopplerPlot | **增强** 2D RD图 (距离轴×多普勒轴)，dB标度 |
| STFT | STFTPlotWidget | 时频分析图 (去载波后) |
| 脉冲对比 | PulseComparePlot | **增强** 目标/干扰/合成三线叠加对比 |
| 拖引轨迹 | TrajectoryPlot | **增强** 距离/速度 vs 脉冲索引 (仅Case5/6) |

**导出功能：**
- PNG 图片导出 (1920px 宽, pyqtgraph ImageExporter)
- SVG 矢量图导出 (pyqtgraph SVGExporter)
- 数据导出 (CSV): echo_target, jam_signal, target_signal

### C++ 调用层 (main_window.py)

`_run_jamming_cpp()` 函数直接调用 `jamming_cpp.run_jamming()`：
- 将扁平参数字典转换为 `system_cfg` / `jamming_cfg` 字典
- 调用 C++ 后端生成干扰
- 返回三个 numpy 复数矩阵 (echo_target, jam_signal, target_signal, 均 nrn×nan1) + log 输出

### Windows 任务栏图标 (main_window.py)

与 GUI_waveform 相同的三要素方案：
1. `SetCurrentProcessExplicitAppUserModelID` — 创建窗口前调用
2. `SetClassLongPtrW(GCLP_HICONSM/HICON)` — 类级别图标设置
3. `QTimer.singleShot(200, ...)` — showEvent 中延迟调用

### 主题管理 (theme.py)

支持亮色/暗色双主题切换（View菜单），暗色使用 Catppuccin Mocha 配色方案。

## 返回数据结构

C++ 后端通过 pybind11 返回以下数据：

| 字段 | 类型 | 说明 |
|------|------|------|
| echo_target | complex128 (nrn×nan1) | 含干扰回波 |
| jam_signal | complex128 (nrn×nan1) | 纯干扰信号 |
| target_signal | complex128 (nrn×nan1) | 纯目标回波 |
| nrn | int | 距离向采样点数 |
| nan1 | int | 方位向脉冲数 |
| log_output | str | C++ 后端日志输出 |

## 配置文件

GUI 按以下顺序搜索 `config.json`：
1. 命令行参数指定路径
2. 环境变量 `SIGNAL_PROC_CONFIG`
3. 当前目录
4. 父目录
5. `~/.signal_processing/config.json`

读取 `system.*` 和 `jamming.*` 配置节。

## 依赖

- Python 3.13+
- PySide6
- pyqtgraph 0.14+
- numpy
- scipy
- pybind11 (编译时)
- PyInstaller (打包时)

## 与 GUI_waveform 的关系

两个 GUI 完全独立，各自封装一个 C++ 模块：

| 特性 | GUI_waveform | GUI_jamming |
|------|-------------|-------------|
| 封装模块 | 模块01 (波形生成) | 模块02 (干扰生成) |
| 模式数 | 5 (Case1~5) | 10 (Case1~10) |
| C++ 绑定 | waveform_cpp.pyd | jamming_cpp.pyd |
| 静态库 | libwaveform_core.a | libjamming_core.a |
| 库依赖 | Eigen | Eigen + FFTW3 |
| 返回数据 | 1个矩阵 + 辅助序列 | 3个矩阵 (target/jam/echo) |
| 图表数 | 6种 | 7种 (含RD图/脉冲对比/拖引轨迹) |

共享组件：theme.py, console_panel.py, scientific_spinbox.py, signal_utils.py, 资源文件。
