@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
cd /d "%~dp0"
set ROOT=%cd%

echo ========================================
echo   Building all 4 GUIs
echo ========================================
echo.

start "01_GUI_waveform" /wait cmd.exe /c "cd /d %ROOT%\01_GUI_waveform && scripts\build.bat"
echo [DONE] 01_GUI_waveform

start "02_GUI_jamming" /wait cmd.exe /c "cd /d %ROOT%\02_GUI_jamming && scripts\build.bat"
echo [DONE] 02_GUI_jamming

start "03_GUI_detection" /wait cmd.exe /c "cd /d %ROOT%\03_GUI_detection && scripts\build.bat"
echo [DONE] 03_GUI_detection

start "04_GUI_signal_processing" /wait cmd.exe /c "cd /d %ROOT%\04_GUI_signal_processing && scripts\build.bat"
echo [DONE] 04_GUI_signal_processing

echo.
echo ========================================
echo   All builds complete
echo ========================================
