import sys
import os


def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import Qt

    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )

    app = QApplication(sys.argv)
    app.setApplicationName("Radar Waveform Generation")
    app.setApplicationVersion("1.0.0")

    from ui.main_window import MainWindow

    config_path = None
    if len(sys.argv) > 1:
        config_path = sys.argv[1]

    window = MainWindow(config_path)
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
