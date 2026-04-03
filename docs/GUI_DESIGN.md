# GUI - 雷达波形生成系统

## 功能概述

基于 PySide6 的雷达波形生成与可视化桌面应用。**仅封装模块01（波形生成）**，通过 pybind11 直接调用模块01的 `libwaveform_core.a` 静态库，支持5种波形模式(Case1~5)的参数配置、波形生成和7种可视化图表。

**注意：** 本GUI不涉及模块02/03/04，仅提供模块01的交互界面。

## 技术架构

```
用户界面 (PySide6 — ui/main_window.py 等)
    ↕
内联调用层 (main_window.py 中的 _run_waveform_cpp)
    ↕ pybind11
C++ 绑定层 (01_waveform_generation/bindings/waveform_bind.cpp → waveform_cpp.so)
    ↕
共享静态库 (01_waveform_generation/src/waveform_core.cpp → libwaveform_core.a)
    ↕
工具层 (Module0.h, Module1.h, parameters.h, Config.h)
```

**核心特点：**
- 不通过文件I/O传递数据，所有波形数据通过内存 numpy 数组直接传递
- C++ 绑定代码位于 `01_waveform_generation/bindings/`，GUI 目录不含 C++ 源码
- 静态库 `libwaveform_core.a` 由模块01构建，GUI 的 pybind11 编译时链接

## 目录结构

```
GUI/
├── app.py                          # 程序入口
├── requirements.txt                # Python 依赖
├── README.md                       # 文档
│
├── scripts/                        # 构建/打包脚本
│   ├── build.sh                    # 一键构建脚本
│   ├── setup_cython.py             # Cython 编译配置
│   └── 雷达波形生成系统.spec         # PyInstaller 打包配置
│
├── ui/                             # PySide6 界面
│   ├── main_window.py              # 主窗口(含 C++ 调用逻辑 _run_waveform_cpp)
│   ├── param_panel.py              # 参数配置面板(系统参数+Case参数)
│   ├── plot_panel.py               # 可视化面板(7种图表)
│   ├── console_panel.py            # 控制台日志面板
│   ├── scientific_spinbox.py       # 科学计数法输入控件
│   └── theme.py                    # 主题管理(亮色/暗色Catppuccin Mocha)
│
├── core/                           # Python 后端模块(Cython 编译后只有 .so)
│   ├── __init__.py
│   ├── config_manager.cpython-*.so # 配置管理(编译后)
│   └── signal_utils.cpython-*.so   # 工具函数(编译后)
│
├── waveform_cpp.cpython-*.so       # C++ 编译产物(链接 01 模块静态库)
├── venv/                           # Python 虚拟环境
└── dist/                           # 构建输出
    ├── config.json                 # 运行时配置文件
    └── 雷达波形生成系统.app          # macOS 应用包
```

## 功能详解

### 参数配置面板 (param_panel.py)

分为系统参数和Case参数两个区域：

**系统参数（所有Case共享）：**

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
| nan1 | 64 | 方位向脉冲数 |

**派生参数（自动计算显示）：** fs=3×B, gama=B/Tp, nrn, lambda, Tstart 等

**Case参数：** 每种波形模式有独立参数面板，包含对应 config.json 中的参数。

### 可视化面板 (plot_panel.py)

支持7种图表标签页：

| 标签 | 类名 | 说明 |
|------|------|------|
| 时域 | TimeDomainPlot | 单脉冲实部/虚部/包络 |
| 频域 | FreqDomainPlot | 频谱幅度(dB) |
| 图像 | ImagePlot | 信号矩阵热力图(实部/虚部/幅度/相位) |
| 频率序列 | FreqSeqPlot | 每脉冲载频柱状图(含十字准线) |
| 随机相位 | PhaseSeqPlot | phi1相位角散点图(Case2/5) |
| STFT | STFTPlot | 时频分析图(去数字载波后) |

### C++ 调用层 (main_window.py)

`main_window.py` 中的 `_run_waveform_cpp()` 函数直接调用 `waveform_cpp.run_waveform()`：
- 将扁平参数字典转换为 `system_cfg` / `waveform_cfg` 字典
- 调用 C++ 后端生成波形
- 返回 numpy 复数矩阵 + 辅助序列 (f, phi1, freq_seq)

### 主题管理 (theme.py)

支持亮色/暗色双主题切换（View菜单），暗色使用 Catppuccin Mocha 配色方案。

### 控制台面板 (console_panel.py)

实时显示C++后端的stdout输出，支持彩色日志（INFO/WARNING/ERROR/SUCCESS）。

## 构建与运行

```bash
cd GUI/

# 开发调试(不打包)
bash scripts/build.sh run

# 一键构建(编译C++ + 打包.app)
bash scripts/build.sh all
```

## 配置文件

GUI使用项目根目录的 `config.json`（通过 `../config.json` 路径访问），参数修改后即时生效。

## 依赖

- Python 3.13+
- PySide6
- pyqtgraph
- numpy
- pybind11 (编译时)

## 与C++模块的关系

```
GUI/app.py
    → ui/main_window.py (_run_waveform_cpp)
        → waveform_cpp.so (pybind11, 位于 GUI/ 目录)
            → 01_waveform_generation/bindings/waveform_bind.cpp
                → libwaveform_core.a (静态库, 来自模块01)
                    → C++ waveform_core.cpp (5种波形生成函数)
                    → C++ Module0.h (FFT/IFFT/窗函数等)
                    → C++ parameters.h (参数定义)
                    → C++ Config.h (config.json读取)
```

GUI直接调用C++函数，生成结果通过内存numpy数组返回给Python进行可视化，不经过文件I/O。
