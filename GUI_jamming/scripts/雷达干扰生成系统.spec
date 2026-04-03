# -*- mode: python ; coding: utf-8 -*-

GUI_ROOT = SPECPATH + '/..'

a = Analysis(
    [GUI_ROOT + '/app.py'],
    pathex=[],
    binaries=[(GUI_ROOT + '/lib/jamming_cpp.cpython-314-darwin.so', '.')],
    datas=[(GUI_ROOT + '/ui', 'ui'), (GUI_ROOT + '/core', 'core'), (GUI_ROOT + '/assets', 'assets')],
    hiddenimports=['scipy.signal', 'scipy.fft', 'scipy', 'PySide6.QtWidgets', 'PySide6.QtCore', 'PySide6.QtGui', 'pyqtgraph', 'pyqtgraph.graphicsItems', 'pyqtgraph.exporters', 'assets'],
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
    name='雷达干扰生成系统',
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
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='雷达干扰生成系统',
)
app = BUNDLE(
    coll,
    name='雷达干扰生成系统.app',
    icon=None,
    bundle_identifier='com.radar.jamming',
)
