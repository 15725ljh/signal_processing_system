# ============================================================================
#  模块01: 波形生成 (Waveform Generation)
#  目标名: waveform_gen
#  源码: src/main.cpp, src/Module1.cpp
#  头文件: include/Config.h, parameters.h, Module0.h, Module1.h
# ============================================================================
#
#  功能: 生成5种模式的雷达发射波形(Case1~5),输出到output/目录
#  依赖: Eigen(矩阵运算), FFTW3(傅里叶变换), 自实现bessel_i0(替代Boost), 自实现JSON解析(替代nlohmann)
#  构建: cmake --build . --target waveform_gen -j8
#  运行: ./build/01_waveform_generation/waveform_gen
# ============================================================================

cmake_minimum_required(VERSION 3.14)    # 最低CMake版本要求

project(WaveformGeneration CXX)        # 模块01项目名称

set(CMAKE_CXX_STANDARD 17)              # C++17标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)     # 强制C++17
set(CMAKE_CXX_EXTENSIONS OFF)           # 禁用编译器扩展,保证跨平台兼容

if(NOT CMAKE_BUILD_TYPE)                # 默认Release模式
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(NOT PROJECT_ROOT_DIR)                # 独立构建时,设置项目根目录并加载库检测脚本
    set(PROJECT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindLibraries.cmake")
endif()

# ── 波形核心静态库 (供本模块和 GUI pybind11 共用) ──
add_library(waveform_core STATIC src/waveform_core.cpp)

target_include_directories(waveform_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include # waveform_core.h / waveform_params.h
    ${EIGEN_INCLUDE_DIR}                # Eigen(矩阵/向量运算)
)

target_compile_features(waveform_core PUBLIC cxx_std_17)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT MSVC)
        target_compile_options(waveform_core PRIVATE -O3)
    endif()
endif()

# ── 可执行目标 waveform_gen ──
file(GLOB_RECURSE MODULE_SOURCES "src/Module*.cpp" "src/main.cpp")  # 模块入口(排除 waveform_core.cpp)

add_executable(waveform_gen ${MODULE_SOURCES})

target_include_directories(waveform_gen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include # 模块本地头文件(Config.h/parameters.h/Module0.h/Module1.h)
    ${NLOHMANN_INCLUDE_DIR}             # nlohmann/json.hpp(JSON配置解析)
    ${EIGEN_INCLUDE_DIR}                # Eigen(矩阵/向量运算)
    ${FFTW_INCLUDE_DIR}                 # FFTW3(快速傅里叶变换)
)

if(FFTW_LIBRARY_DIR)                    # FFTW库目录(可能为空,如系统默认路径)
    target_link_directories(waveform_gen PRIVATE ${FFTW_LIBRARY_DIR})
endif()
target_link_libraries(waveform_gen PRIVATE fftw3 waveform_core) # 链接FFTW3和波形核心静态库(已移除Boost依赖)

if(CMAKE_BUILD_TYPE STREQUAL "Release") # Release模式下启用最高优化
    if(MSVC)                            # MSVC编译器: /O2优化
        target_compile_options(waveform_gen PRIVATE /O2)
    else()                              # GCC/Clang: -O3优化
        target_compile_options(waveform_gen PRIVATE -O3)
    endif()
endif()

target_compile_definitions(waveform_gen PRIVATE OUTPUT_DIR="${PROJECT_ROOT_DIR}/output/") # 输出目录宏定义
