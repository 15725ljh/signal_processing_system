# 信号处理系统

## 项目概述

本项目包含四个独立的雷达信号处理流程，使用 C++17、Eigen、FFTW 和自实现的 mini JSON 解析器实现。所有模块支持外部 JSON 配置文件参数化，无需重新编译即可调整参数。

## 四大流程模块

| 模块 | 目录 | 功能 | 模式数 |
|------|------|------|--------|
| 波形生成 | `01_waveform_generation` (加密 zip) | LFM/NLFM/相位编码/FM/单频波形 | 5 |
| 干扰生成 | `02_jamming_generation` (加密 zip) | RDJ/VDJ/ISRJ/NBJ/拖引/假目标等干扰 | 10 |
| 干扰识别与抑制 | `03_jamming_detection_suppression` (加密 zip) | ISDJ/ISRJ/ISCJ/NBJ/RDJ识别+时频抑制 | 5 |
| 信号处理 | `04_signal_processing` (加密 zip) | 干扰识别+6种处理算法 | 6 |

> **注意**：C++ 源码已从仓库移除，备份为加密 zip（密码见 [BUILD_GUIDE.md](BUILD_GUIDE.md)）。解压后即可编译。

## 技术栈

- C++17
- Eigen 3.4.0 (线性代数)
- FFTW 3.3.10 (快速傅里叶变换)
- 自实现 mini JSON 解析器 (~215行, 替代 nlohmann/json 24765行)
- 自实现 `bessel_i0()` Taylor 级数 (替代 Boost `cyl_bessel_i`)
- CMake 3.14+

## 配置文件系统

所有模块通过统一的 `config.json` 进行参数配置：

- **智能寻址**: 环境变量 `SPS_CONFIG` → 可执行文件同目录 → 项目根目录 → `~/.config/sps/config.json`
- **安全容错**: 未配置的参数自动使用代码内置默认值，`null` 值等同于未设置
- **注释支持**: JSON文件内支持 `//` 和 `#` 行注释
- **热生效**: 修改 config.json 后重新运行程序即可，无需重新编译

### 配置参数分区

| 分区 | key路径前缀 | 适用模块 | 说明 |
|------|------------|---------|------|
| 系统参数 | `system.*` | 01/02/04 | 载波频率、带宽、PRF等全局参数 |
| 波形生成 | `waveform.*` | 01 | 跳频点数、抖动范围等 |
| 干扰生成 | `jamming.*` | 02 | 10种干扰模式各自的参数 |
| 信号处理 | `processing.*` | 04 | Kaiser窗、STFT、Tsallis参数 |
| 干扰识别 | `recognition.*` | 04 | STFT频点数、阈值系数等 |
| 检测抑制 | `detection_suppression.*` | 03 | 独立参数体系(fc=35GHz) |

## 构建

> C++ 源码需先解压加密 zip（密码见 [BUILD_GUIDE.md](BUILD_GUIDE.md)），详见 [跨平台编译运行手册](BUILD_GUIDE.md)。

各模块独立构建，每个模块有自己的 `build/` 目录：

```bash
# 解压 C++ 源码后编译
cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../02_jamming_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../03_jamming_detection_suppression && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../04_signal_processing && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# GUI (Python, 无需 C++ 源码)
cd 01_GUI_waveform && pip install -r requirements.txt && python app.py
cd 02_GUI_jamming && pip install -r requirements.txt && python app.py
cd 03_GUI_detection && pip install -r requirements.txt && python app.py
cd 04_GUI_signal_processing && pip install -r requirements.txt && python app.py
```

## 目录结构

```
signal_processing_system/
├── config.json                 # 外部参数配置文件(含详细注释)
├── CMakeLists.txt              # C++ 顶层构建配置
├── build_all.bat               # 一键构建全部 4 个 GUI exe
├── docs/                       # 统一文档目录
│   ├── README.md               # 项目概述(本文件)
│   ├── BUILD_GUIDE.md          # 跨平台编译运行手册
│   ├── WAVEFORM_GUI_DESIGN.md           # 波形生成 GUI 设计文档
│   ├── JAMMING_GUI_DESIGN.md            # 干扰生成 GUI 设计文档
│   ├── DETECTION_GUI_DESIGN.md          # 干扰识别 GUI 设计文档
│   ├── SIGNAL_PROCESSING_GUI_DESIGN.md  # 信号处理 GUI 设计文档
│   ├── 01_waveform_generation.md
│   ├── 02_jamming_generation.md
│   ├── 03_jamming_detection_suppression.md
│   └── 04_signal_processing.md
├── third_party/                # 本地第三方库
│   ├── nlohmann/json.hpp       # 自实现迷你JSON解析器(~215行)
│   ├── eigen/                  # Eigen 3.4.0 (header-only)
│   └── fftw-install/           # FFTW 3.3.10 (需编译)
├── cmake/                      # CMake 模块 (FindLibraries.cmake)
├── 01_waveform_generation.zip     # 模块1 C++ 源码加密备份
├── 02_jamming_generation.zip      # 模块2 C++ 源码加密备份
├── 03_jamming_detection_suppression.zip  # 模块3 C++ 源码加密备份
├── 04_signal_processing.zip       # 模块4 C++ 源码加密备份
├── 01_GUI_waveform/               # PySide6 GUI (波形生成, 链接 libwaveform_core.a)
├── 02_GUI_jamming/                # PySide6 GUI (干扰生成, 链接 libjamming_core.a)
├── 03_GUI_detection/              # PySide6 GUI (干扰识别, 链接 libdetection_core.a)
├── 04_GUI_signal_processing/      # PySide6 GUI (信号处理, 链接 libsignal_processing_core.a + libwaveform_core.a)
├── build/                      # C++ 构建输出目录
└── output/                     # 统一输出目录
```

## 输出

所有模块的输出文件统一存放到 `output/` 目录。

## 模块间数据流

```
模块01 (波形生成)                    模块02 (干扰生成)
├── 01_Case1_信号矩阵_signal.dat    ├── 02_Case{1-10}_含干扰回波_jammed.dat
├── 01_Case1_跳频序列_freq_hop.dat  └── 02_Case{1-10}_干扰信号.dat
├── ...
└── ...
         │
         └──── 文件传递 ──────────→  模块04 (信号处理)
                                    shibie() ← 读模块02数据
                                    Case1~5 ← 读模块01数据
                                    Case6   ← 读模块02数据

模块03 (干扰检测抑制) ← 完全独立，不读写其他模块文件

01_GUI_waveform ──pybind11──→ 模块01 (内存 numpy 数组)
02_GUI_jamming  ──pybind11──→ 模块02 (内存 numpy 数组)
03_GUI_detection ──pybind11──→ 模块03 (内存 numpy 数组)
04_GUI_signal_processing ──pybind11──→ 模块04+01 (内存 numpy 数组)
  四个 GUI 独立运行，不读写 .dat 文件
```

**运行顺序建议**: 01 → 02 → 04 (模块03可独立运行)

## 详细文档

- [跨平台编译运行手册](BUILD_GUIDE.md)
- [01 - 波形生成模块](01_waveform_generation.md)
- [02 - 干扰生成模块](02_jamming_generation.md)
- [03 - 干扰识别与抑制模块](03_jamming_detection_suppression.md)
- [04 - 信号处理模块](04_signal_processing.md)
- [波形生成 GUI 设计](WAVEFORM_GUI_DESIGN.md)
- [干扰生成 GUI 设计](JAMMING_GUI_DESIGN.md)
- [干扰识别 GUI 设计](DETECTION_GUI_DESIGN.md)
- [信号处理 GUI 设计](SIGNAL_PROCESSING_GUI_DESIGN.md)
