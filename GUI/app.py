import sys
import os
import base64 as _b64


def _assets_dir():
    """Return the path to the assets directory (works both in-source and PyInstaller-frozen)."""
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'assets')
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), 'assets')


def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QIcon, QPixmap

    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )

    app = QApplication(sys.argv)
    app.setApplicationName("Radar Waveform Generation")
    app.setApplicationVersion("1.0.0")

    # Windows 11 任务栏图标: 必须在创建窗口前设置 AppUserModelID
    # 否则任务栏会缓存默认图标且不响应 WM_SETICON
    if sys.platform == 'win32':
        try:
            import ctypes
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID.restype = ctypes.c_int
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID.argtypes = [ctypes.c_wchar_p]
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("XDU_LJH.RadarWaveformGen")
        except Exception:
            pass

    # 从外部文件加载图标
    _icon = QIcon()
    try:
        _b64_path = os.path.join(_assets_dir(), 'icon_b64.txt')
        with open(_b64_path, 'r', encoding='utf-8') as _f:
            _icon_b64 = _f.read().strip()
        _png = _b64.b64decode(_icon_b64)
        _pm = QPixmap()
        _pm.loadFromData(_png)
        if not _pm.isNull():
            for _s in [16, 24, 32, 48, 64, 128, 256]:
                _icon.addPixmap(_pm.scaled(_s, _s, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
            app.setWindowIcon(_icon)
    except Exception:
        pass

    from ui.main_window import MainWindow

    config_path = None
    if len(sys.argv) > 1:
        config_path = sys.argv[1]

    window = MainWindow(config_path)
    window.setWindowIcon(_icon)
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
