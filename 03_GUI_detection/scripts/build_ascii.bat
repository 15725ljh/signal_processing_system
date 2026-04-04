@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0\.."
set GUI_ROOT=%cd%
set PY=%GUI_ROOT%\venv\Scripts\python.exe

echo === [1/3] C++ binding (detection_cpp.pyd) ===

for /f "delims=" %%i in ('%PY% -c "import pybind11; print(pybind11.get_include())"') do set PYBIND_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_path('include'))"') do set PYTHON_INC=%%i
for /f "delims=" %%i in ('%PY% -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))"') do set PYTHON_LIB=%%i

set MODULE03_LIB=%GUI_ROOT%\..\03_jamming_detection_suppression\build\libdetection_core.a
if not exist "%MODULE03_LIB%" (
    echo [ERROR] libdetection_core.a not found
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
    echo [ERROR] C++ build failed
    exit /b 1
)
echo [OK] lib\detection_cpp.pyd

echo === [2/3] Cython ===
if exist core\config_manager.py (
    %PY% scripts\setup_cython.py build_ext --inplace
    del core\config_manager.py core\signal_utils.py core\*.c 2>nul
    rmdir /s /q core\__pycache__
    echo [OK] core/*.pyd
) else (
    echo [SKIP] core/ already compiled
)

echo === [3/3] PyInstaller ===
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist
%PY% -m PyInstaller --clean scripts\build_win.spec

if errorlevel 1 (
    echo [ERROR] PyInstaller failed
    exit /b 1
)
echo.
echo ========================================
echo   DONE: dist\...exe
echo ========================================
