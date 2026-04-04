# -*- mode: python ; coding: utf-8 -*-
"""
Windows 版 PyInstaller 打包配置 — 雷达信号处理系统 (模块04)

用法:
    cd GUI_signal_processing
    venv\Scripts\activate
    pyinstaller --clean scripts/雷达信号处理系统_win.spec

输出: dist/雷达信号处理系统/雷达信号处理系统.exe
"""

import sys
import glob

GUI_ROOT = SPECPATH + '/..'

# 自动查找 .pyd 文件名（版本号因 Python 版本不同）
cpp_module = glob.glob(GUI_ROOT + '/lib/signal_processing_cpp*.pyd')
if not cpp_module:
    print("[错误] 未找到 signal_processing_cpp*.pyd，请先编译 C++ 绑定")
    print("       在 Developer Command Prompt 中运行: scripts\\build.bat")
    sys.exit(1)

print(f"[信息] C++ 模块: {cpp_module[0]}")

# MinGW UCRT64 运行时 DLL (signal_processing_cpp.pyd 的依赖)
mingw_dlls = [f for f in glob.glob(GUI_ROOT + '/lib/lib*.dll')]
if not mingw_dlls:
    print("[警告] 未找到 lib*.dll MinGW 运行时, 打包后可能无法运行")
binaries = [(cpp_module[0], '.')] + [(f, '.') for f in mingw_dlls]

a = Analysis(
    [GUI_ROOT + '/app.py'],
    pathex=[],
    binaries=binaries,
    datas=[
        (GUI_ROOT + '/ui', 'ui'),
        (GUI_ROOT + '/core', 'core'),
        (GUI_ROOT + '/assets', 'assets'),
    ],
    hiddenimports=[
        'scipy.signal', 'scipy.fft', 'scipy',
        'PySide6.QtWidgets', 'PySide6.QtCore', 'PySide6.QtGui',
        'pyqtgraph', 'pyqtgraph.graphicsItems', 'pyqtgraph.exporters',
        'assets',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'tkinter', 'unittest', 'email', 'html', 'xml', 'pydoc',
        'doctest', 'difflib', 'inspect', 'asyncio', 'multiprocessing',
        'lib2to3', 'setuptools', 'pip', 'pytest',
        'IPython', 'jupyter', 'notebook', 'matplotlib',
        'pandas', 'flask', 'django', 'requests', 'urllib3',
    ],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='雷达信号处理系统',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=GUI_ROOT + '/assets/app_icon.ico',
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='雷达信号处理系统',
)
