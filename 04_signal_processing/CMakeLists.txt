cmake_minimum_required(VERSION 3.14)

project(SignalProcessing CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

add_compile_definitions(_USE_MATH_DEFINES)  # Windows下启用M_PI等数学常量

if(NOT PROJECT_ROOT_DIR)                # 独立构建时,设置项目根目录并加载库检测脚本
    set(PROJECT_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindLibraries.cmake")
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "test_case6\\.cpp$")

add_executable(signal_proc ${SOURCES})

# Standalone test for Case6 (jamTarDivi separation quality)
add_executable(test_case6 src/test_case6.cpp)
target_include_directories(test_case6 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NLOHMANN_INCLUDE_DIR}
    ${EIGEN_INCLUDE_DIR}
    ${FFTW_INCLUDE_DIR}
)
if(FFTW_LIBRARY_DIR)
    target_link_directories(test_case6 PRIVATE ${FFTW_LIBRARY_DIR})
endif()
target_link_libraries(test_case6 PRIVATE fftw3)

target_include_directories(signal_proc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${NLOHMANN_INCLUDE_DIR}
    ${EIGEN_INCLUDE_DIR}
    ${FFTW_INCLUDE_DIR}
)

if(FFTW_LIBRARY_DIR)
    target_link_directories(signal_proc PRIVATE ${FFTW_LIBRARY_DIR})
endif()
target_link_libraries(signal_proc PRIVATE fftw3)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(signal_proc PRIVATE /O2)
    else()
        target_compile_options(signal_proc PRIVATE -O3)
    endif()
endif()

target_compile_definitions(signal_proc PRIVATE OUTPUT_DIR="${PROJECT_ROOT_DIR}/output/")
