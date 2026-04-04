# ============================================================================
#  模块03: 干扰识别与抑制 (Jamming Detection & Suppression)
#  目标名: jamming_det_sup
#  源码: src/main.cpp
#  头文件: include/Config.h, TfrStft.h, JamTarDivi.h, EchoGenerator.h,
#          JammingSimulator.h, GrDetection.h, JamLocated.h,
#          CountDuplicateVectors.h, SignalWriter.h
# ============================================================================
#
#  功能: 基于 STFT 时频分析识别5种干扰类型(窄带/扫频/梳状/噪声/间歇),
#        并计算干扰抑制比(JSR).独立参数体系(fc=35GHz, B=80MHz, R0=1000m, prf=5kHz)
#  依赖: Eigen(矩阵运算), FFTW3(傅里叶变换), 自实现JSON解析(替代nlohmann)
#         可选: OpenMP(多线程并行加速)
#  构建: cmake --build . --target jamming_det_sup -j8
#  运行: ./build/03_jamming_detection_suppression/jamming_det_sup
#
#  注意: 本模块参数体系与01/02/04模块(fc=16GHz)完全独立
# ============================================================================

cmake_minimum_required(VERSION 3.14)    # 最低CMake版本要求

project(JammingDetectionSuppression CXX) # 模块03项目名称

set(CMAKE_CXX_STANDARD 17)              # C++17标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)     # 强制C++17
set(CMAKE_CXX_EXTENSIONS OFF)           # 禁用编译器扩展,保证跨平台兼容

if(NOT CMAKE_BUILD_TYPE)                # 默认Release模式
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

add_compile_definitions(_USE_MATH_DEFINES)  # Windows下启用M_PI等数学常量

# OpenMP多线程支持(可选,用于并行化STFT计算等密集操作)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP enabled")
    set(OPENMP_ENABLED ON)
else()
    message(STATUS "OpenMP not found, using single-threaded mode")
    set(OPENMP_ENABLED OFF)
endif()

if(NOT PROJECT_ROOT_DIR)                # 独立构建时,设置项目根目录并加载库检测脚本
    set(PROJECT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindLibraries.cmake")
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")  # 递归收集src/下所有.cpp源文件

# 静态库目标: 供 03_GUI_detection 通过 pybind11 链接
add_library(detection_core STATIC
    src/detection_core.cpp
)
target_include_directories(detection_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
    ${FFTW_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
)
target_link_libraries(detection_core PRIVATE fftw3)
if(OpenMP_CXX_FOUND)
    target_link_libraries(detection_core PRIVATE OpenMP::OpenMP_CXX)
endif()

add_executable(jamming_det_sup ${SOURCES}) # 构建可执行目标 jamming_det_sup

target_include_directories(jamming_det_sup PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include # 模块本地头文件(Config.h/TfrStft.h/JamTarDivi.h等)
    ${EIGEN_INCLUDE_DIR}                # Eigen(矩阵/向量运算)
    ${FFTW_INCLUDE_DIR}                 # FFTW3(快速傅里叶变换)
    ${NLOHMANN_INCLUDE_DIR}             # nlohmann/json.hpp(JSON配置解析)
)

if(FFTW_LIBRARY_DIR)                    # FFTW库目录(可能为空,如系统默认路径)
    target_link_directories(jamming_det_sup PRIVATE ${FFTW_LIBRARY_DIR})
endif()

target_link_libraries(jamming_det_sup PRIVATE fftw3) # 仅链接FFTW3(本模块不使用Boost)

if(OpenMP_CXX_FOUND)                    # 若检测到OpenMP,则链接(并行加速STFT等)
    target_link_libraries(jamming_det_sup PRIVATE OpenMP::OpenMP_CXX)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release") # Release模式下启用最高优化
    if(MSVC)                            # MSVC编译器: /O2优化
        target_compile_options(jamming_det_sup PRIVATE /O2)
    else()                              # GCC/Clang: -O3优化
        target_compile_options(jamming_det_sup PRIVATE -O3)
    endif()
endif()

target_compile_definitions(jamming_det_sup PRIVATE OUTPUT_DIR="${PROJECT_ROOT_DIR}/output/") # 输出目录宏定义
