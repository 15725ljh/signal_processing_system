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

    # 将 lib/ 目录加入 sys.path, 以便 import waveform_cpp
    _lib_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lib')
    if os.path.isdir(_lib_dir):
        sys.path.insert(0, _lib_dir)

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

    # 加载应用图标
    _icon = QIcon()
    try:
        _png_path = os.path.join(_assets_dir(), 'app_icon.png')
        if os.path.exists(_png_path):
            _icon = QIcon(_png_path)
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
