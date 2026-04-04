@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

set ROOT=C:\Code\C++\signal_processing_system

echo ========================================
echo   Building all 4 GUIs in parallel
echo ========================================
echo.

start "GUI_waveform" /wait cmd.exe /c "cd /d %ROOT%\GUI_waveform && scripts\build.bat"
echo [DONE] GUI_waveform

start "GUI_jamming" /wait cmd.exe /c "cd /d %ROOT%\GUI_jamming && scripts\build.bat"
echo [DONE] GUI_jamming

start "GUI_detection" /wait cmd.exe /c "cd /d %ROOT%\GUI_detection && scripts\build.bat"
echo [DONE] GUI_detection

start "GUI_signal_processing" /wait cmd.exe /c "cd /d %ROOT%\GUI_signal_processing && scripts\build.bat"
echo [DONE] GUI_signal_processing

echo.
echo ========================================
echo   All builds complete
echo ========================================
