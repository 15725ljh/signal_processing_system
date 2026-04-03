# ============================================================================
#  模块02: 干扰生成 (Jamming Generation)
#  目标名: jamming_gen
#  源码: src/main.cpp, src/Module2.cpp
#  头文件: include/Config.h, parameters.h, Module0.h, Module2.h
# ============================================================================
#
#  功能: 生成10种模式的雷达干扰信号(Case1~10),输出到output/目录
#  依赖: Eigen(矩阵运算), FFTW3(傅里叶变换), 自实现bessel_i0(替代Boost), 自实现JSON解析(替代nlohmann)
#  构建: cmake --build . --target jamming_gen -j8
#  运行: ./build/02_jamming_generation/jamming_gen
# ============================================================================

cmake_minimum_required(VERSION 3.14)    # 最低CMake版本要求

project(JammingGeneration CXX)         # 模块02项目名称

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

file(GLOB_RECURSE SOURCES "src/*.cpp")  # 递归收集src/下所有.cpp源文件

add_executable(jamming_gen ${SOURCES})  # 构建可执行目标 jamming_gen

target_include_directories(jamming_gen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include # 模块本地头文件(Config.h/parameters.h/Module0.h/Module2.h)
    ${NLOHMANN_INCLUDE_DIR}             # nlohmann/json.hpp(JSON配置解析)
    ${EIGEN_INCLUDE_DIR}                # Eigen(矩阵/向量运算)
    ${FFTW_INCLUDE_DIR}                 # FFTW3(快速傅里叶变换)
)

if(FFTW_LIBRARY_DIR)                    # FFTW库目录(可能为空,如系统默认路径)
    target_link_directories(jamming_gen PRIVATE ${FFTW_LIBRARY_DIR})
endif()
target_link_libraries(jamming_gen PRIVATE fftw3) # 链接FFTW3(已移除Boost依赖)

if(CMAKE_BUILD_TYPE STREQUAL "Release") # Release模式下启用最高优化
    if(MSVC)                            # MSVC编译器: /O2优化
        target_compile_options(jamming_gen PRIVATE /O2)
    else()                              # GCC/Clang: -O3优化
        target_compile_options(jamming_gen PRIVATE -O3)
    endif()
endif()

target_compile_definitions(jamming_gen PRIVATE OUTPUT_DIR="${PROJECT_ROOT_DIR}/output/") # 输出目录宏定义

# ── 静态库目标: jamming_core (供 GUI pybind11 绑定使用) ──
# 不依赖 Config.h / parameters.h / Module0.h, 所有参数通过 JammingParams 传入
add_library(jamming_core STATIC src/jamming_core.cpp)

target_include_directories(jamming_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # jamming_core.h, jamming_params.h
    ${EIGEN_INCLUDE_DIR}
    ${FFTW_INCLUDE_DIR}
)

if(FFTW_LIBRARY_DIR)
    target_link_directories(jamming_core PRIVATE ${FFTW_LIBRARY_DIR})
endif()
target_link_libraries(jamming_core PRIVATE fftw3)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(jamming_core PRIVATE /O2)
    else()
        target_compile_options(jamming_core PRIVATE -O3)
    endif()
endif()
