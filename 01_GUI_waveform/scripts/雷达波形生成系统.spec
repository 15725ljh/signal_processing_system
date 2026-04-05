# -*- mode: python ; coding: utf-8 -*-
"""
macOS 版 PyInstaller 打包配置 — 雷达波形生成系统

用法:
    cd 01_GUI_waveform
    source venv/bin/activate
    pyinstaller --clean scripts/雷达波形生成系统.spec

输出: dist/雷达波形生成系统.app
"""

import sys
import glob

GUI_ROOT = SPECPATH + '/..'

# 自动查找 .so 文件名
cpp_module = glob.glob(GUI_ROOT + '/lib/waveform_cpp*.so')
if not cpp_module:
    print("[错误] 未找到 waveform_cpp*.so，请先编译 C++ 绑定")
    print("       bash scripts/build.sh cpp")
    sys.exit(1)

print(f"[信息] C++ 模块: {cpp_module[0]}")

a = Analysis(
    [GUI_ROOT + '/app.py'],
    pathex=[],
    binaries=[(cpp_module[0], '.')],
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
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='雷达波形生成系统',
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
    icon=GUI_ROOT + '/assets/app_icon.icns' if sys.platform == 'darwin' else GUI_ROOT + '/assets/app_icon.ico',
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='雷达波形生成系统',
)
app = BUNDLE(
    coll,
    name='雷达波形生成系统.app',
    icon=GUI_ROOT + '/assets/app_icon.icns' if sys.platform == 'darwin' else None,
    bundle_identifier='com.radar.waveform',
)
