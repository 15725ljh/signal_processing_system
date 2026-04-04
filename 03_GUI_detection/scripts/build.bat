@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."
set GUI_ROOT=%cd%
set PY=%GUI_ROOT%\venv\Scripts\python.exe

echo === [1/3] 编译 C++ 绑定 (detection_cpp.pyd) ===

for /f "delims=" %%i in ('%PY% -c "import pybind11; print(pybind11.get_include())"') do set PYBIND_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_path('include'))"') do set PYTHON_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))"') do set PYTHON_LIB=%%i

set MODULE03_LIB=%GUI_ROOT%\..\03_jamming_detection_suppression\build\libdetection_core.a
if not exist "%MODULE03_LIB%" (
    echo [错误] 未找到 libdetection_core.a，请先构建模块03
    echo        cd 03_jamming_detection_suppression ^&^& mkdir build ^&^& cd build ^&^& cmake .. -G "MinGW Makefiles" ^&^& cmake --build . --target detection_core
    exit /b 1
)

if not exist lib mkdir lib

g++ -O3 -std=c++17 -shared ^
    -I"%PYBIND_INC%" ^
    -I"%PYTHON_INC%" ^
    -I"%GUI_ROOT%\..\third_party\eigen" ^
    -I"%GUI_ROOT%\..\third_party\fftw-install\include" ^
    -I"%GUI_ROOT%\..\third_party\nlohmann" ^
    -I"%GUI_ROOT%\..\03_jamming_detection_suppression\include" ^
    "%GUI_ROOT%\..\03_jamming_detection_suppression\bindings\detection_bind.cpp" ^
    "%MODULE03_LIB%" ^
    -L"%GUI_ROOT%\..\third_party\fftw-install\lib" -lfftw3 ^
    -L"%PYTHON_LIB%" -lpython313 ^
    -o lib\detection_cpp.pyd

if errorlevel 1 (
    echo [错误] C++ 编译失败
    exit /b 1
)
echo [完成] lib\detection_cpp.pyd

echo === [2/3] 编译 Cython 模块 ===
if exist core\config_manager.py (
    %PY% scripts\setup_cython.py build_ext --inplace
    if errorlevel 1 (
        echo [错误] Cython 编译失败
        exit /b 1
    )
    del core\config_manager.py core\signal_utils.py core\*.c 2>nul
    rmdir /s /q core\__pycache__
    echo [完成] core/*.pyd
) else (
    echo [跳过] core/ 已是编译产物
)

echo === [3/3] 打包 .exe ===
if "%~1"=="clean" (
    if exist build rmdir /s /q build
    if exist dist  rmdir /s /q dist
    %PY% -m PyInstaller --clean --noconfirm scripts\雷达干扰识别系统_win.spec
) else (
    %PY% -m PyInstaller --noconfirm scripts\雷达干扰识别系统_win.spec
)

if errorlevel 1 (
    echo [错误] 打包失败
    exit /b 1
)
echo.
echo ========================================
echo   完成: dist\雷达干扰识别系统.exe
echo ========================================
pause
