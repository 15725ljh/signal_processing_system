# 信号处理系统

## 项目概述

本项目包含四个独立的雷达信号处理流程，使用 C++17、Eigen、FFTW 和自实现的 mini JSON 解析器实现。所有模块支持外部 JSON 配置文件参数化，无需重新编译即可调整参数。

## 四大流程模块

| 模块 | 目录 | 功能 | 模式数 |
|------|------|------|--------|
| 波形生成 | `01_waveform_generation` | LFM/NLFM/相位编码/FM/单频波形 | 5 |
| 干扰生成 | `02_jamming_generation` | RDJ/VDJ/ISRJ/NBJ/拖引/假目标等干扰 | 10 |
| 干扰识别与抑制 | `03_jamming_detection_suppression` | ISDJ/ISRJ/ISCJ/NBJ/RDJ识别+时频抑制 | 5 |
| 信号处理 | `04_signal_processing` | 干扰识别+6种处理算法 | 6 |

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

各模块独立构建，每个模块有自己的 `build/` 目录：

```bash
# 独立构建各模块
cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../02_jamming_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../03_jamming_detection_suppression && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)
cd ../../04_signal_processing && mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# 或统一构建（从项目根目录）
mkdir -p build && cd build && cmake .. && cmake --build . -j$(sysctl -n hw.ncpu)

# 运行(从项目根目录)
cd ../build
./01_waveform_generation/waveform_gen
./02_jamming_generation/jamming_gen
./03_jamming_detection_suppression/jamming_det_sup
./04_signal_processing/signal_proc
```

## 目录结构

```
signal_processing_system/
├── CMakeLists.txt              # 顶层构建文件(add_subdirectory ×4)
├── config.json                 # 外部参数配置文件(含详细注释)
├── docs/                       # 统一文档目录
│   ├── README.md               # 项目概述(本文件)
│   ├── BUILD_GUIDE.md          # 跨平台编译运行手册
│   ├── WAVEFORM_GUI_DESIGN.md           # 波形生成 GUI 设计文档
│   ├── JAMMING_GUI_DESIGN.md            # 干扰生成 GUI 设计文档
│   ├── 01_waveform_generation.md
│   ├── 02_jamming_generation.md
│   ├── 03_jamming_detection_suppression.md
│   └── 04_signal_processing.md
├── cmake/
│   └── FindLibraries.cmake     # 共享库检测逻辑(Eigen/FFTW/nlohmann)
├── common/
│   └── Config.h                # 配置管理器(单例，智能寻址，注释剥离)
├── third_party/                # 本地第三方库
│   ├── nlohmann/json.hpp       # 自实现迷你JSON解析器(~215行)
│   ├── eigen/                  # Eigen 3.4.0 (header-only)
│   └── fftw-install/           # FFTW 3.3.10 (需编译)
├── 01_waveform_generation/     # 模块1: 波形生成(5种模式) + libwaveform_core.a
│   ├── include/                # 头文件(parameters.h/Module0.h/Module1.h/waveform_core.h/waveform_params.h)
│   ├── src/                    # 源文件(main.cpp/Module1.cpp/waveform_core.cpp)
│   └── bindings/               # pybind11 绑定(waveform_bind.cpp, 供GUI使用)
├── 02_jamming_generation/      # 模块2: 干扰生成(10种模式) + libjamming_core.a
│   ├── include/                # 头文件(parameters.h/Module0.h/Module2.h/jamming_core.h/jamming_params.h)
│   ├── src/                    # 源文件(main.cpp/Module2.cpp/jamming_core.cpp)
│   └── bindings/               # pybind11 绑定(jamming_bind.cpp, 供GUI使用)
├── 03_jamming_detection_suppression/ # 模块3: 干扰识别与抑制(5种类型)
├── 04_signal_processing/       # 模块4: 信号处理(6种模式)
├── GUI_waveform/               # PySide6 GUI(链接模块01静态库)
├── GUI_jamming/                # PySide6 GUI(链接模块02静态库)
├── GUI_detection/              # PySide6 GUI(链接模块03静态库)
├── GUI_signal_processing/      # PySide6 GUI(链接模块04+01静态库)
└── output/                     # 统一输出目录
```

## 输出

所有模块的输出文件统一存放到 `output/` 目录。

## 模块间数据流

```
模块01 (波形生成)                    模块02 (干扰生成)
├── 01_Case1_信号矩阵_signal.dat    ├── 02_Case{1-10}_含干扰回波_jammed.dat
├── 01_Case1_跳频序列_freq_hop.dat  └── 02_Case{1-10}_干扰信号.dat
├── 01_Case2_信号矩阵_signal.dat
├── 01_Case2_随机相位序列.dat        注: Case1-3(转发型)内部生成目标回波
├── 01_Case3_信号矩阵_signal.dat          Case4-10(生成型)自行产生干扰
├── 01_Case4_信号矩阵_signal.dat          所有Case均不依赖模块01的输入文件
├── 01_Case4_等效载频序列.dat              │
├── 01_Case5_信号矩阵_signal.dat          │ 文件传递
├── 01_Case5_跳频序列_freq_hop.dat        ▼
└── 01_Case5_随机相位序列.dat       ┌──────────────────────────┐
         │                          │  模块04 (信号处理)        │
         └──── 文件传递 ──────────→ │  shibie() ← 读模块02数据 │
                                    │  Case1~5 ← 读模块01数据  │
                                    │  Case6   ← 读模块02数据  │
                                    │  (兜底: EchoGenerator4)  │
                                    └──────────────────────────┘

模块03 (干扰检测抑制) ← 完全独立，不读写其他模块文件
  fc=35GHz, B=80MHz, R0=1000m, nrn=2048
  自含 EchoGenerator + JammingSimulator

GUI_waveform (PySide6界面) ← 封装模块01(波形生成)，通过pybind11直接调用C++
  GUI_jamming (PySide6界面) ← 封装模块02(干扰生成)，通过pybind11直接调用C++
  GUI_detection (PySide6界面) ← 封装模块03(干扰识别与抑制)，通过pybind11直接调用C++
  GUI_signal_processing (PySide6界面) ← 封装模块04(信号处理)+模块01(波形生成)，通过pybind11直接调用C++
  四个GUI独立运行，不读写.dat文件，数据通过内存numpy数组传递
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
