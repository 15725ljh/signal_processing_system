@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."
set GUI_ROOT=%cd%
set PY=%GUI_ROOT%\venv\Scripts\python.exe

echo === [1/4] 编译 C++ 绑定 (signal_processing_cpp.pyd) ===

for /f "delims=" %%i in ('%PY% -c "import pybind11; print(pybind11.get_include())"') do set PYBIND_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_path('include'))"') do set PYTHON_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))"') do set PYTHON_LIB=%%i

set MODULE04_LIB=%GUI_ROOT%\..\04_signal_processing\build\libsignal_processing_core.a
set MODULE01_LIB=%GUI_ROOT%\..\01_waveform_generation\build\libwaveform_core.a

if not exist "%MODULE04_LIB%" (
    echo [错误] 未找到 libsignal_processing_core.a，请先构建模块04
    echo        cd 04_signal_processing ^&^& mkdir build ^&^& cd build ^&^& cmake .. -G "MinGW Makefiles" ^&^& cmake --build . --target signal_processing_core
    exit /b 1
)

if not exist "%MODULE01_LIB%" (
    echo [错误] 未找到 libwaveform_core.a，请先构建模块01
    echo        cd 01_waveform_generation ^&^& mkdir build ^&^& cd build ^&^& cmake .. -G "MinGW Makefiles" ^&^& cmake --build . --target waveform_core
    exit /b 1
)

if not exist lib mkdir lib

g++ -O3 -std=c++17 -shared ^
    -I"%PYBIND_INC%" ^
    -I"%PYTHON_INC%" ^
    -I"%GUI_ROOT%\..\third_party\eigen" ^
    -I"%GUI_ROOT%\..\third_party\fftw-install\include" ^
    -I"%GUI_ROOT%\..\third_party" ^
    -I"%GUI_ROOT%\..\04_signal_processing\include" ^
    -I"%GUI_ROOT%\..\01_waveform_generation\include" ^
    "%GUI_ROOT%\..\04_signal_processing\bindings\signal_processing_bind.cpp" ^
    "%MODULE04_LIB%" "%MODULE01_LIB%" ^
    -L"%GUI_ROOT%\..\third_party\fftw-install\lib" -lfftw3 ^
    -L"%PYTHON_LIB%" -lpython313 ^
    -o lib\signal_processing_cpp.pyd

if errorlevel 1 (
    echo [错误] C++ 编译失败
    exit /b 1
)
echo [完成] lib\signal_processing_cpp.pyd

echo === [2/4] 编译 Cython 模块 ===
if exist core\config_manager.py (
    %PY% scripts\setup_cython.py build_ext --inplace
    del core\config_manager.py core\signal_utils.py core\*.c 2>nul
    rmdir /s /q core\__pycache__
    echo [完成] core/*.pyd
) else (
    echo [跳过] core/ 已是编译产物
)

echo === [3/4] 打包 .exe ===
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist
%PY% -m PyInstaller --clean scripts\雷达信号处理系统_win.spec

if errorlevel 1 (
    echo [错误] 打包失败
    exit /b 1
)
echo.
echo ========================================
echo   完成: dist\雷达信号处理系统\雷达信号处理系统.exe
echo ========================================
pause
