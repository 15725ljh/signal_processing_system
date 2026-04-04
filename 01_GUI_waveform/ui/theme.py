import os
import sys

FONT_UI = '"PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Noto Sans CJK SC", sans-serif'
FONT_MONO = '"Menlo", "PingFang SC", "Consolas", "SF Mono", monospace'


def _assets_dir():
    """定位 assets 目录, 兼容 PyInstaller 打包和源码运行"""
    if getattr(sys, 'frozen', False):
        base = sys._MEIPASS
    else:
        base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, 'assets')


_CHECK_SVG = os.path.join(_assets_dir(), 'checkmark.svg').replace('\\', '/')
_CHEVRON_SVG = os.path.join(_assets_dir(), 'chevron-down.svg').replace('\\', '/')

# ── Plot theme palettes (used by plot_panel.py) ──
PLOT_THEMES = {
    "light": {
        "pg_bg": "#ffffff",
        "pg_fg": "#2c3e50",
        "viewbox_bg": "#fafbfd",
        "grid": "#e8ecf0",
        "axis": "#bdc3c7",
        "text": "#5a6a7a",
        "crosshair": "#ffffff",
        "crosshair_text": "#2c3e50",
        "crosshair_fill": (255, 255, 255, 180),
        "blue": "#3478f6",
        "red": "#e04040",
        "green": "#2ea44f",
        "purple": "#8250df",
        "orange": "#d47216",
        "teal": "#1a8a7d",
    },
    "dark": {
        "pg_bg": "#1e1e2e",
        "pg_fg": "#cdd6f4",
        "viewbox_bg": "#181825",
        "grid": "#313244",
        "axis": "#585b70",
        "text": "#a6adc8",
        "crosshair": "#cdd6f4",
        "crosshair_text": "#cdd6f4",
        "crosshair_fill": (30, 30, 46, 200),
        "blue": "#89b4fa",
        "red": "#f38ba8",
        "green": "#a6e3a1",
        "purple": "#cba6f7",
        "orange": "#fab387",
        "teal": "#94e2d5",
    },
}

# ── Widget theme palettes (used by console_panel.py, param_panel.py) ──
WIDGET_THEMES = {
    "light": {
        "console_title_fg": "#7f8c8d",
        "console_line_fg": "#95a5a6",
        "derived_label_fg": "#5b8def",
        "derived_label_bg": "#eef2f7",
        "console_log_colors": {
            "info": "#2c3e50",
            "header": "#2c5aa0",
            "success": "#27855a",
            "warning": "#b8860b",
            "error": "#c0392b",
            "dim": "#95a5a6",
        },
    },
    "dark": {
        "console_title_fg": "#a6adc8",
        "console_line_fg": "#6c7086",
        "derived_label_fg": "#89b4fa",
        "derived_label_bg": "#313244",
        "console_log_colors": {
            "info": "#cdd6f4",
            "header": "#89b4fa",
            "success": "#a6e3a1",
            "warning": "#f9e2af",
            "error": "#f38ba8",
            "dim": "#6c7086",
        },
    },
}


def _build_style(colors):
    """Build a Qt stylesheet from a color dict."""
    c = colors
    return f"""
QMainWindow, QDialog {{
    background-color: {c['bg']};
    color: {c['fg']};
    font-family: {FONT_UI};
    font-size: 13px;
}}

QWidget {{
    background-color: {c['bg']};
    color: {c['fg']};
    font-family: {FONT_UI};
    font-size: 13px;
}}

QGroupBox {{
    border: 1px solid {c['border']};
    border-radius: 10px;
    margin-top: 16px;
    padding: 14px 10px 10px 10px;
    font-weight: bold;
    font-size: 13px;
    color: {c['fg']};
    background-color: {c['group_bg']};
}}

QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 4px 14px;
    color: {c['title_fg']};
    background-color: {c['title_bg']};
    border-radius: 6px;
    font-size: 12px;
}}

QLabel {{
    color: {c['fg']};
    background: transparent;
    padding: 1px 2px;
    font-size: 13px;
}}

QLabel[label_type="title"] {{
    font-size: 14px;
    font-weight: bold;
    color: {c['title_bg']};
}}

QLabel[label_type="dim"] {{
    color: {c['dim']};
    font-size: 11px;
}}

QLabel[label_type="sep"] {{
    color: {c['sep']};
    font-size: 11px;
}}

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {{
    background-color: {c['input_bg']};
    border: 1.5px solid {c['border']};
    border-radius: 6px;
    padding: 5px 10px;
    color: {c['fg']};
    font-family: {FONT_MONO};
    font-size: 12px;
    min-height: 24px;
    selection-background-color: {c['select_bg']};
}}

QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {{
    border: 1.5px solid {c['hover_border']};
}}

QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {{
    border: 1.5px solid {c['accent']};
    background-color: {c['input_bg']};
}}

QComboBox::drop-down {{
    border: none;
    width: 24px;
}}

QComboBox::down-arrow {{
    image: url({_CHEVRON_SVG});
    width: 12px;
    height: 12px;
}}

QComboBox QAbstractItemView {{
    background-color: {c['input_bg']};
    color: {c['fg']};
    selection-background-color: {c['select_bg']};
    selection-color: {c['fg']};
    border: 1px solid {c['border']};
    border-radius: 6px;
    font-family: {FONT_MONO};
    font-size: 12px;
    padding: 4px;
}}

QPushButton {{
    background-color: {c['btn_bg']};
    border: 1.5px solid {c['border']};
    border-radius: 8px;
    padding: 7px 20px;
    color: {c['fg']};
    font-weight: bold;
    font-size: 13px;
    min-height: 26px;
}}

QPushButton:hover {{
    background-color: {c['btn_hover']};
    border: 1.5px solid {c['hover_border']};
}}

QPushButton:pressed {{
    background-color: {c['btn_pressed']};
}}

QPushButton:disabled {{
    background-color: {c['btn_disabled']};
    color: {c['disabled_fg']};
    border: 1.5px solid {c['disabled_border']};
}}

QPushButton#actionRun {{
    background-color: {c['run_bg']};
    color: #ffffff;
    border: none;
    font-size: 13px;
    font-weight: bold;
    padding: 8px 20px;
    min-height: 32px;
    min-width: 100px;
    border-radius: 8px;
}}

QPushButton#actionRun:hover {{
    background-color: {c['run_hover']};
}}

QPushButton#actionRun:pressed {{
    background-color: {c['run_pressed']};
}}

QPushButton#actionRun:disabled {{
    background-color: {c['run_disabled']};
    color: #ffffff;
}}

QPushButton#actionStop {{
    background-color: {c['stop_bg']};
    color: #ffffff;
    border: none;
    font-size: 13px;
    font-weight: bold;
    padding: 8px 20px;
    min-height: 32px;
    min-width: 100px;
    border-radius: 8px;
}}

QPushButton#actionStop:hover {{
    background-color: {c['stop_hover']};
}}

QPushButton#actionStop:pressed {{
    background-color: {c['stop_pressed']};
}}

QPushButton#actionStop:disabled {{
    background-color: {c['stop_disabled']};
    color: #ffffff;
}}

QPushButton#actionRestore {{
    background-color: {c['restore_bg']};
    color: #ffffff;
    border: none;
    font-size: 13px;
    font-weight: bold;
    padding: 8px 20px;
    min-height: 32px;
    min-width: 100px;
    border-radius: 8px;
}}

QPushButton#actionRestore:hover {{
    background-color: {c['restore_hover']};
}}

QPushButton#actionRestore:pressed {{
    background-color: {c['restore_pressed']};
}}

QPushButton#actionClear {{
    background-color: {c['clear_bg']};
    color: #ffffff;
    border: none;
    font-size: 13px;
    font-weight: bold;
    padding: 8px 20px;
    min-height: 32px;
    min-width: 100px;
    border-radius: 8px;
}}

QPushButton#actionClear:hover {{
    background-color: {c['clear_hover']};
}}

QPushButton#actionClear:pressed {{
    background-color: {c['clear_pressed']};
}}

QPushButton#smallButton {{
    background-color: {c['btn_bg']};
    border: 1px solid {c['border']};
    padding: 5px 14px;
    min-height: 22px;
    font-size: 12px;
    border-radius: 6px;
    color: {c['fg']};
}}

QPushButton#smallButton:hover {{
    background-color: {c['btn_hover']};
    border: 1px solid {c['hover_border']};
}}

QPushButton#selectAllBtn {{
    background-color: {c['selectall_bg']};
    border: none;
    padding: 5px 14px;
    min-height: 22px;
    font-size: 12px;
    border-radius: 6px;
    color: #ffffff;
    font-weight: bold;
}}

QPushButton#selectAllBtn:hover {{
    background-color: {c['selectall_hover']};
}}

QPushButton#deselectAllBtn {{
    background-color: {c['deselectall_bg']};
    border: none;
    padding: 5px 14px;
    min-height: 22px;
    font-size: 12px;
    border-radius: 6px;
    color: #ffffff;
    font-weight: bold;
}}

QPushButton#deselectAllBtn:hover {{
    background-color: {c['deselectall_hover']};
}}

QPushButton#lockToggle {{
    background-color: {c['btn_bg']};
    border: 1.5px solid {c['border']};
    padding: 6px 14px;
    font-size: 13px;
    font-weight: bold;
    border-radius: 6px;
    color: {c['fg']};
}}
QPushButton#lockToggle:hover {{
    background-color: {c['btn_hover']};
    border: 1.5px solid {c['accent']};
    color: {c['accent']};
}}
QPushButton#lockToggle:checked {{
    background-color: {c['accent']};
    border: 1.5px solid {c['accent']};
    color: #ffffff;
}}

QTabWidget::pane {{
    border: 1px solid {c['border']};
    border-radius: 8px;
    background-color: {c['group_bg']};
    top: -1px;
}}

QTabBar::tab {{
    background-color: {c['tab_bg']};
    border: 1px solid {c['border']};
    border-bottom: none;
    padding: 8px 24px;
    margin-right: 2px;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
    color: {c['dim']};
    font-size: 13px;
    min-width: 70px;
}}

QTabBar::tab:selected {{
    background-color: {c['tab_sel_bg']};
    color: {c['accent']};
    border-bottom: 2px solid {c['accent']};
    font-weight: bold;
}}

QTabBar::tab:hover:!selected {{
    background-color: {c['tab_sel_bg']};
    color: {c['fg']};
}}

QPlainTextEdit {{
    background-color: {c['console_bg']};
    border: 1px solid {c['border']};
    border-radius: 8px;
    color: {c['fg']};
    font-family: {FONT_MONO};
    font-size: 12px;
    padding: 8px;
    line-height: 1.5;
}}

QScrollBar:vertical {{
    border: none;
    background: {c['scroll_bg']};
    width: 10px;
    margin: 0;
    border-radius: 5px;
}}

QScrollBar::handle:vertical {{
    background: {c['scroll_handle']};
    border-radius: 5px;
    min-height: 30px;
}}

QScrollBar::handle:vertical:hover {{
    background: {c['scroll_hover']};
}}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
    height: 0;
}}

QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{
    background: none;
}}

QScrollBar:horizontal {{
    border: none;
    background: {c['scroll_bg']};
    height: 10px;
    border-radius: 5px;
}}

QScrollBar::handle:horizontal {{
    background: {c['scroll_handle']};
    border-radius: 5px;
    min-width: 30px;
}}

QScrollBar::handle:horizontal:hover {{
    background: {c['scroll_hover']};
}}

QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {{
    background: none;
}}

QCheckBox {{
    color: {c['fg']};
    spacing: 8px;
    font-size: 13px;
}}

QCheckBox::indicator {{
    width: 20px;
    height: 20px;
    border: 2px solid {c['check_border']};
    border-radius: 5px;
    background-color: {c['input_bg']};
}}

QCheckBox::indicator:checked {{
    background-color: {c['accent']};
    border-color: {c['accent']};
    image: url({_CHECK_SVG});
}}

QCheckBox::indicator:hover {{
    border-color: {c['accent']};
}}

QMenuBar {{
    background-color: {c['menubar_bg']};
    border-bottom: 1px solid {c['menubar_border']};
    color: {c['fg']};
    font-size: 13px;
    padding: 2px;
}}

QMenuBar::item {{
    padding: 6px 16px;
    border-radius: 6px;
}}

QMenuBar::item:selected {{
    background-color: {c['select_bg']};
}}

QMenu {{
    background-color: {c['group_bg']};
    border: 1px solid {c['border']};
    border-radius: 8px;
    padding: 6px;
}}

QMenu::item {{
    padding: 8px 32px;
    border-radius: 6px;
    font-size: 13px;
}}

QMenu::item:selected {{
    background-color: {c['select_bg']};
}}

QMenu::separator {{
    height: 1px;
    background: {c['menubar_border']};
    margin: 5px 12px;
}}

QStatusBar {{
    background-color: {c['menubar_bg']};
    border-top: 1px solid {c['menubar_border']};
    color: {c['dim']};
    font-size: 12px;
    padding: 2px 10px;
}}

QSplitter::handle {{
    background-color: {c['menubar_border']};
}}

QSplitter::handle:horizontal {{
    width: 3px;
}}

QSplitter::handle:vertical {{
    height: 3px;
}}

QSplitter::handle:hover {{
    background-color: {c['accent']};
}}

QScrollArea {{
    border: none;
    background: transparent;
}}

QToolTip {{
    background-color: {c['tooltip_bg']};
    color: {c['tooltip_fg']};
    border: none;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}}

QProgressBar {{
    background-color: {c['progress_bg']};
    border: none;
    border-radius: 8px;
    text-align: center;
    color: {c['fg']};
    min-height: 18px;
    font-size: 12px;
}}

QProgressBar::chunk {{
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 {c['run_bg']}, stop:1 {c['restore_bg']});
    border-radius: 8px;
}}
"""


# ── Light theme colors ──
_LIGHT = {
    "bg": "#f8f9fc", "fg": "#2c3e50",
    "group_bg": "#ffffff", "border": "#dce1e8",
    "title_fg": "#ffffff", "title_bg": "#5b8def",
    "input_bg": "#ffffff",
    "hover_border": "#a8c4e0", "accent": "#5b8def",
    "select_bg": "#e8f0fe",
    "dim": "#7f8c8d", "sep": "#bdc3c7",
    "btn_bg": "#ffffff", "btn_hover": "#eef2f7", "btn_pressed": "#dce6f0",
    "btn_disabled": "#f0f2f5", "disabled_fg": "#bdc3c7", "disabled_border": "#e8ecf0",
    "run_bg": "#5b8def", "run_hover": "#4a7cde", "run_pressed": "#3f6fcf", "run_disabled": "#b0c4e8",
    "stop_bg": "#e86c6c", "stop_hover": "#d65b5b", "stop_pressed": "#c44a4a", "stop_disabled": "#e8c4c4",
    "restore_bg": "#4caf82", "restore_hover": "#3d9e72", "restore_pressed": "#358d65",
    "clear_bg": "#f0a654", "clear_hover": "#e09540", "clear_pressed": "#d08530",
    "tab_bg": "#eef2f7", "tab_sel_bg": "#ffffff",
    "console_bg": "#ffffff",
    "scroll_bg": "#f0f2f5", "scroll_handle": "#c4cdd8", "scroll_hover": "#a0aab8",
    "check_border": "#c4cdd8",
    "menubar_bg": "#ffffff", "menubar_border": "#e8ecf0",
    "tooltip_bg": "#34495e", "tooltip_fg": "#ffffff",
    "progress_bg": "#e8ecf0",
    "selectall_bg": "#4caf82", "selectall_hover": "#3d9e72",
    "deselectall_bg": "#8e99a4", "deselectall_hover": "#7f8c99",
}

# ── Dark theme colors (Catppuccin Mocha) ──
_DARK = {
    "bg": "#1e1e2e", "fg": "#cdd6f4",
    "group_bg": "#313244", "border": "#45475a",
    "title_fg": "#1e1e2e", "title_bg": "#89b4fa",
    "input_bg": "#313244",
    "hover_border": "#585b70", "accent": "#89b4fa",
    "select_bg": "#45475a",
    "dim": "#a6adc8", "sep": "#585b70",
    "btn_bg": "#313244", "btn_hover": "#45475a", "btn_pressed": "#585b70",
    "btn_disabled": "#313244", "disabled_fg": "#585b70", "disabled_border": "#45475a",
    "run_bg": "#89b4fa", "run_hover": "#74c7ec", "run_pressed": "#65b1e0", "run_disabled": "#45475a",
    "stop_bg": "#f38ba8", "stop_hover": "#eb7fa0", "stop_pressed": "#d67290", "stop_disabled": "#45475a",
    "restore_bg": "#a6e3a1", "restore_hover": "#94d996", "restore_pressed": "#84cf88",
    "clear_bg": "#fab387", "clear_hover": "#f0a570", "clear_pressed": "#e09660",
    "tab_bg": "#1e1e2e", "tab_sel_bg": "#313244",
    "console_bg": "#181825",
    "scroll_bg": "#313244", "scroll_handle": "#585b70", "scroll_hover": "#6c7086",
    "check_border": "#585b70",
    "menubar_bg": "#1e1e2e", "menubar_border": "#313244",
    "tooltip_bg": "#45475a", "tooltip_fg": "#cdd6f4",
    "progress_bg": "#313244",
    "selectall_bg": "#a6e3a1", "selectall_hover": "#94d996",
    "deselectall_bg": "#6c7086", "deselectall_hover": "#7f849c",
}

LIGHT_STYLE = _build_style(_LIGHT)
DARK_STYLE = _build_style(_DARK)
