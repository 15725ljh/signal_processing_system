<div align="center">

# Radar Signal Processing System

**雷达信号处理系统 — 波形生成 · 干扰模拟 · 检测抑制 · 信号处理**

[![Language](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/std/the-standard)
[![Framework](https://img.shields.io/badge/CMake-3.14+-green.svg)](https://cmake.org/)
[![GUI](https://img.shields.io/badge/GUI-PySide6-orange.svg)](https://doc.qt.io/qtforpython-6/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

---

## 项目简介

一套完整的雷达电子对抗信号处理系统，涵盖 **波形生成、干扰模拟、干扰识别与抑制、信号处理** 四大核心流程。支持 10 种干扰模式（转发型 + 生成型）的仿真与 6 种信号处理算法，所有参数通过外部 JSON 配置，无需重新编译。

## 系统架构

```
┌─────────────────┐    ┌─────────────────┐
│   模块 01       │    │   模块 02       │
│   波形生成      │    │   干扰生成      │
│   (5 种模式)    │    │   (10 种模式)   │
│                 │    │                 │
│ · LFM/NLFM     │    │ · 距离假目标(RDJ)│
│ · 跳频/相位编码 │    │ · 速度假目标(VDJ)│
│ · PRI 抖动     │    │ · 间歇采样(ISRJ) │
│ · 混合波形     │    │ · 窄带噪声(NNJ)  │
│ · 复合波形     │    │ · RGPOJ/VGPOJ   │
└────────┬────────┘    │ · DRFTJ/IPLE    │
         │             │ · SMSPJ/COMBJ   │
         │             └────────┬────────┘
         │                      │
         └──────┐  ┌────────────┘
                │  │
         ┌──────▼──▼──────┐    ┌─────────────────┐
         │   模块 04       │    │   模块 03       │
         │   信号处理      │    │   干扰检测抑制   │
         │   (6 种模式)    │    │   (5 种类型)    │
         │                 │    │                 │
         │ · 匹配滤波      │    │ · ISDJ/ISRJ    │
         │ · STFT 分析     │    │ · ISCJ/NBJ     │
         │ · 干扰识别      │    │ · RDJ 检测     │
         │ · 时频抑制      │    │ · 目标/干扰分离 │
         │ · Tsallis 处理  │    │ · 时频域抑制    │
         └─────────────────┘    └─────────────────┘
              (独立运行，不依赖其他模块)
```

## 技术特性

- **高精度信号模型** — 采用 `frac(x)` 分数部分提取技术，避免 `fc·t ≈ 10⁶` 时的浮点精度丢失
- **热配置** — 修改 `config.json` 即时生效，支持 `//` 和 `#` 行注释
- **级联处理** — 模块间通过 `.dat` 文件传递数据，模块 02 支持 10 种干扰的级联叠加
- **跨平台** — 支持 macOS / Linux / Windows (MSYS2)
- **GUI 界面** — PySide6 桌面应用，pybind11 直调 C++ 引擎，实时波形可视化
- **轻量依赖** — 仅依赖 Eigen 和 FFTW 两个第三方库，JSON 解析和 Bessel 函数为自实现

## 快速开始

```bash
# 1. 克隆仓库
git clone https://github.com/15725ljh/signal_processing_system.git
cd signal_processing_system

# 2. 安装依赖 (macOS 示例)
brew install eigen fftw

# 3. 构建各模块（独立构建）
cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../02_jamming_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../03_jamming_detection_suppression && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../04_signal_processing && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# 或统一构建
cd signal_processing_system && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# 4. 运行
cd ../build
./01_waveform_generation/waveform_gen
./02_jamming_generation/jamming_gen
./03_jamming_detection_suppression/jamming_det_sup
./04_signal_processing/signal_proc
```

> 详细构建指南见 [docs/BUILD_GUIDE.md](docs/BUILD_GUIDE.md)

## 技术栈

| 组件 | 技术 | 用途 |
|------|------|------|
| 语言 | C++17 | 核心信号处理算法 |
| 线性代数 | Eigen 3.4.0 | 矩阵运算 |
| FFT | FFTW 3.3.10 | 快速傅里叶变换 |
| 配置解析 | 自实现 mini JSON (~215行) | JSON 参数文件解析 |
| Bessel 函数 | 自实现 `bessel_i0()` Taylor 级数 | Kaiser 窗函数 |
| 构建系统 | CMake 3.14+ | 跨平台构建 |
| GUI | PySide6 + pybind11 | 波形可视化界面 |

## 10 种干扰模式

| 类型 | 模式 | 缩写 | 原理 |
|------|------|------|------|
| 转发型 | Case 1 | RDJ | 距离假目标 — FFT + 频域时延相位 + IFFT |
| 转发型 | Case 2 | VDJ | 速度假目标 — 多普勒调制 + FFT + 时延相位 |
| 转发型 | Case 3 | ISRJ | 间歇采样转发 — 周期采样窗 + 多普勒切片重组 |
| 生成型 | Case 4 | NNJ | 窄带噪声 — 复高斯白噪声 + 载波调制 + 低通滤波 |
| 生成型 | Case 5 | RGPOJ | 距离波门拖引 — 三阶段策略(捕获→拖引→反转) |
| 生成型 | Case 6 | VGPOJ | 速度波门拖引 — 多普勒频移渐进变化 |
| 生成型 | Case 7 | DRFTJ | 密集复制转发 — 50 个等间隔假目标 |
| 生成型 | Case 8 | IPLESRJ | 脉内前沿切片重复 — 16 次转发等间隔假目标群 |
| 生成型 | Case 9 | SMSPJ | 频谱弥散 — 增强调频率子脉冲匹配滤波失配 |
| 生成型 | Case 10 | COMBJ | 梳状谱 — 7 个离散频率点对称分布 |

## 目录结构

```
signal_processing_system/
├── CMakeLists.txt              # 顶层构建入口(add_subdirectory ×4)
├── config.json                 # 外部参数配置文件(所有模块共享)
├── cmake/
│   └── FindLibraries.cmake     # 库自动检测(Eigen/FFTW/nlohmann)
├── common/
│   └── Config.h                # 配置管理器(单例，JSON解析，智能寻址)
├── third_party/
│   ├── nlohmann/json.hpp       # 自实现迷你JSON解析器(~215行)
│   ├── eigen/                  # Eigen 3.4.0 (header-only)
│   └── fftw-install/           # FFTW 3.3.10 (需编译)
├── 01_waveform_generation/     # 模块1: 波形生成 + libwaveform_core.a 静态库
├── 02_jamming_generation/      # 模块2: 干扰生成(10种模式)
├── 03_jamming_detection_suppression/ # 模块3: 干扰识别与抑制(5种类型)
├── 04_signal_processing/       # 模块4: 信号处理(6种模式)
├── GUI/                        # PySide6 GUI (链接模块01静态库)
├── docs/                       # 统一文档目录
└── output/                     # 运行输出目录
```

## 文档

| 文档 | 说明 |
|------|------|
| [构建指南](docs/BUILD_GUIDE.md) | 跨平台编译运行手册 |
| [波形生成](docs/01_waveform_generation.md) | 模块 01 详解 |
| [干扰生成](docs/02_jamming_generation.md) | 模块 02 详解 |
| [检测抑制](docs/03_jamming_detection_suppression.md) | 模块 03 详解 |
| [信号处理](docs/04_signal_processing.md) | 模块 04 详解 |
| [GUI 设计](docs/GUI_DESIGN.md) | PySide6 界面文档 |

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
