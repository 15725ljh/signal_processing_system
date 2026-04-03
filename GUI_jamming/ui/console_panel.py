from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPlainTextEdit,
    QPushButton, QLabel,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QTextCharFormat, QColor, QFont

from ui.theme import WIDGET_THEMES


class ConsolePanel(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._max_lines = 5000
        self._auto_scroll = True
        self._current_theme = "light"
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        self._title = QLabel("  控制台输出")
        self._title.setStyleSheet("font-weight: bold; font-size: 12px;")
        toolbar.addWidget(self._title)
        toolbar.addStretch()

        self._line_label = QLabel("行数: 0")
        self._line_label.setStyleSheet("font-size: 11px;")
        toolbar.addWidget(self._line_label)

        clear_btn = QPushButton("清空")
        clear_btn.setObjectName("smallButton")
        clear_btn.setFixedWidth(56)
        clear_btn.clicked.connect(self.clear)
        toolbar.addWidget(clear_btn)

        layout.addLayout(toolbar)

        self.text_edit = QPlainTextEdit()
        self.text_edit.setReadOnly(True)
        self.text_edit.setMaximumBlockCount(self._max_lines)
        self.text_edit.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)

        font = QFont("Menlo", 12)
        font.setStyleHint(QFont.StyleHint.Monospace)
        self.text_edit.setFont(font)

        self._init_formats("light")

        layout.addWidget(self.text_edit)

    def _init_formats(self, theme):
        wt = WIDGET_THEMES.get(theme, WIDGET_THEMES["light"])
        colors = wt["console_log_colors"]

        self._fmt_info = QTextCharFormat()
        self._fmt_info.setForeground(QColor(colors["info"]))

        self._fmt_header = QTextCharFormat()
        self._fmt_header.setForeground(QColor(colors["header"]))
        self._fmt_header.setFontWeight(QFont.Weight.Bold)

        self._fmt_success = QTextCharFormat()
        self._fmt_success.setForeground(QColor(colors["success"]))

        self._fmt_warning = QTextCharFormat()
        self._fmt_warning.setForeground(QColor(colors["warning"]))

        self._fmt_error = QTextCharFormat()
        self._fmt_error.setForeground(QColor(colors["error"]))

        self._fmt_dim = QTextCharFormat()
        self._fmt_dim.setForeground(QColor(colors["dim"]))

        wt = WIDGET_THEMES.get(theme, WIDGET_THEMES["light"])
        self._title.setStyleSheet(f"color: {wt['console_title_fg']}; font-weight: bold; font-size: 12px;")
        self._line_label.setStyleSheet(f"color: {wt['console_line_fg']}; font-size: 11px;")

    def apply_theme(self, theme="light"):
        """Update console colors when theme changes."""
        self._current_theme = theme
        self._init_formats(theme)

    def append(self, text, level="info"):
        fmt_map = {
            "info": self._fmt_info,
            "header": self._fmt_header,
            "success": self._fmt_success,
            "warning": self._fmt_warning,
            "error": self._fmt_error,
            "dim": self._fmt_dim,
        }
        fmt = fmt_map.get(level, self._fmt_info)

        cursor = self.text_edit.textCursor()
        cursor.movePosition(cursor.MoveOperation.End)
        cursor.insertText(text + "\n", fmt)
        if self._auto_scroll:
            sb = self.text_edit.verticalScrollBar()
            sb.setValue(sb.maximum())
        self._line_label.setText(f"行数: {self.text_edit.blockCount()}")

    def clear(self):
        self.text_edit.clear()
        self._line_label.setText("行数: 0")
