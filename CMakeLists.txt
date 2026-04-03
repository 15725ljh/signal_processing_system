# ============================================================================
#  顶层 CMakeLists.txt — 雷达信号处理系统总构建入口
#  Top-level build configuration for the Signal Processing System
# ============================================================================
#
#  本文件是整个项目的构建入口,负责:
#    1. 定义全局C++标准(C++17)和构建类型(Release)
#    2. 通过 cmake/FindLibraries.cmake 检测并配置第三方库路径(Eigen/FFTW/nlohmann)
#    3. 按顺序添加四个独立子模块:
#       - 01_waveform_generation    波形生成(5种模式)
#       - 02_jamming_generation     干扰生成(10种模式)
#       - 03_jamming_detection_suppression  干扰识别与抑制(5种类型,fc=35GHz独立参数体系)
#       - 04_signal_processing      干扰识别+信号处理(6种模式)
#
#  构建方式 — 各模块独立构建:
#    cd 01_waveform_generation && mkdir -p build && cd build && cmake .. && cmake --build . -j8
#    cd 02_jamming_generation  && mkdir -p build && cd build && cmake .. && cmake --build . -j8
#    cd 03_jamming_detection_suppression && mkdir -p build && cd build && cmake .. && cmake --build . -j8
#    cd 04_signal_processing   && mkdir -p build && cd build && cmake .. && cmake --build . -j8
#
#  或统一构建(从项目根目录):
#    mkdir -p build && cd build && cmake .. && cmake --build . -j8
#
#  可选构建目标: waveform_gen, waveform_core, jamming_gen, jamming_det_sup, signal_proc
# ============================================================================

cmake_minimum_required(VERSION 3.14)    # 最低CMake版本要求(支持FetchContent等现代特性)

project(SignalProcessingSystem CXX)     # 项目名称,仅使用C++语言

set(CMAKE_CXX_STANDARD 17)              # C++17标准(支持filesystem/optional/string_view等)
set(CMAKE_CXX_STANDARD_REQUIRED ON)     # 强制要求C++17,不支持则报错
set(CMAKE_CXX_EXTENSIONS OFF)           # 禁用编译器扩展(如GNU扩展),保证跨平台兼容

if(NOT CMAKE_BUILD_TYPE)                # 若未指定构建类型,默认使用Release(启用-O3优化)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

include(cmake/FindLibraries.cmake)      # 检测第三方库路径(Eigen/FFTW/nlohmann),设置PROJECT_ROOT_DIR

add_subdirectory(01_waveform_generation)                  # 模块01: 波形生成,目标名 waveform_gen (内含 waveform_core 静态库)
add_subdirectory(02_jamming_generation)                   # 模块02: 干扰生成,目标名 jamming_gen
add_subdirectory(03_jamming_detection_suppression)        # 模块03: 干扰识别与抑制,目标名 jamming_det_sup
add_subdirectory(04_signal_processing)                    # 模块04: 信号处理,目标名 signal_proc
