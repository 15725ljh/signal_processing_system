"""
主窗口 — 信号处理 GUI (模块04)

两个 C++ 入口:
  1. run_processing_rd(case_num, config_dict)        → ProcessingResultRD (Cases 1-5)
  2. run_processing_decouple(jam_type, config_dict)  → ProcessingResultDecouple (Case 6)
"""

import json
import os
import shutil
import sys
import time
import numpy as np
from PySide6.QtWidgets import (
    QMainWindow, QSplitter, QFileDialog, QMenu, QMessageBox,
    QStatusBar, QLabel, QWidget, QVBoxLayout, QProgressBar,
)
from PySide6.QtCore import Qt, QThread, Signal, QTimer
from PySide6.QtGui import QAction, QKeySequence, QFont, QIcon

from ui.theme import LIGHT_STYLE, DARK_STYLE, _assets_dir
from ui.param_panel import ParamPanel
from ui.plot_panel import PlotPanel, apply_plot_theme
from ui.console_panel import ConsolePanel
from core.config_manager import ConfigManager

import signal_processing_cpp


# ── C++ 调用包装 ──

def _run_processing_rd_cpp(case_num, config_dict):
    result = signal_processing_cpp.run_processing_rd(case_num, config_dict)
    return {
        "rd_map":       result["rd_map"],
        "xi":           result["xi"],
        "dv":           result["dv"],
        "input_signal": result["input_signal"],
        "nrn":          result["nrn"],
        "nan1":         result["nan1"],
        "case_num":     result["case_num"],
        "elapsed":      result["elapsed"],
        "log_output":   result.get("log_output", ""),
    }


def _run_processing_decouple_cpp(jam_type, config_dict):
    result = signal_processing_cpp.run_processing_decouple(jam_type, config_dict)
    return {
        "jam_signal":      result["jam_signal"],
        "target_signal":   result["target_signal"],
        "input_signal":    result["input_signal"],
        "decouple_flag":   result["decouple_flag"],
        "jsr_dB":          result["jsr_dB"],
        "avg_threshold":   result["avg_threshold"],
        "gaojiepu_count":  result["gaojiepu_count"],
        "nrn":             result["nrn"],
        "nan1":            result["nan1"],
        "elapsed":         result["elapsed"],
        "log_output":      result.get("log_output", ""),
    }


_JAMMING_TYPE_NAMES = {
    1: "ISDJ 间歇采样直接转发",
    2: "ISRJ 间歇采样重复转发",
    3: "ISCJ 间歇采样循环转发",
    4: "NBJ 窄带瞄频噪声",
    5: "RDJ 距离欺骗干扰",
}

_FUNC_DISPLAY_NAMES = {
    "processing_rd":      "距离-多普勒处理",
    "processing_decouple": "时频干扰解耦",
}


class ComputeThread(QThread):
    logSignal = Signal(str, str)
    resultSignal = Signal(str, int, object)    # 每完成一个 case 发出
    allFinishedSignal = Signal()               # 全部完成
    errorSignal = Signal(str)

    def __init__(self, modes, config_dict, parent=None):
        super().__init__(parent)
        self._modes = modes              # [(func_type, arg), ...]
        self._config_dict = config_dict

    def run(self):
        try:
            total = len(self._modes)
            for i, (func_type, arg) in enumerate(self._modes):
                display_name = _FUNC_DISPLAY_NAMES.get(func_type, func_type)
                self.logSignal.emit(f"[{i+1}/{total}] 开始: {display_name}", "header")

                t0 = time.time()

                if func_type == "processing_rd":
                    self.logSignal.emit(f"Case {arg}", "info")
                    result = _run_processing_rd_cpp(arg, self._config_dict)
                elif func_type == "processing_decouple":
                    jam_name = _JAMMING_TYPE_NAMES.get(arg, f"Type {arg}")
                    self.logSignal.emit(f"Case 6 解耦, 干扰: {arg} ({jam_name})", "info")
                    result = _run_processing_decouple_cpp(arg, self._config_dict)
                else:
                    self.errorSignal.emit(f"未知功能类型: {func_type}")
                    return

                elapsed_wall = time.time() - t0

                if result.get("log_output"):
                    for line in result["log_output"].split("\n"):
                        if line.strip():
                            self.logSignal.emit(line, "info")

                self.logSignal.emit(f"完成 (耗时 {elapsed_wall*1000:.1f} ms)", "success")

                self.resultSignal.emit(func_type, arg, result)

            self.allFinishedSignal.emit()

        except Exception as e:
            self.errorSignal.emit(str(e))


class MainWindow(QMainWindow):

    def __init__(self, config_path=None):
        super().__init__()
        self.setWindowTitle("雷达信号处理系统 - 模块04")
        icon_path = os.path.join(_assets_dir(), 'app_icon.ico')
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))
        self.setMinimumSize(1280, 800)
        self._win32_icon_applied = False
        self.resize(1500, 920)

        self._config = ConfigManager()
        self._compute_thread = None
        self._last_result = None
        self._last_func_type = None
        self._last_arg = None
        self._current_theme = "light"

        self._setup_style()
        self._setup_menu()
        self._setup_ui()
        self._setup_statusbar()

        if config_path:
            self._load_config(config_path)
        else:
            self._auto_load_config()

        self._clock_timer = QTimer()
        self._clock_timer.timeout.connect(self._update_clock)
        self._clock_timer.start(1000)

    # ── Style / Theme ──

    def _setup_style(self):
        self.setStyleSheet(LIGHT_STYLE)
        font = QFont("PingFang SC", 11)
        font.setStyleHint(QFont.StyleHint.SansSerif)
        self.setFont(font)

    def _toggle_theme(self, dark):
        theme = "dark" if dark else "light"
        self._current_theme = theme
        style = DARK_STYLE if dark else LIGHT_STYLE
        self.setStyleSheet(style)
        apply_plot_theme(theme)
        self._console_panel.apply_theme(theme)
        for lbl in self._param_panel._derived_labels.values():
            lbl.apply_theme(theme)
        if self._last_result:
            self._plot_panel.refresh_current()

    # ── Menu ──

    def _setup_menu(self):
        menubar = self.menuBar()

        file_menu = menubar.addMenu("文件(&F)")

        load_action = QAction("加载配置...", self)
        load_action.setShortcut(QKeySequence.Open)
        load_action.triggered.connect(self._on_load_config)
        file_menu.addAction(load_action)

        save_action = QAction("保存配置...", self)
        save_action.setShortcut("Ctrl+Shift+S")
        save_action.triggered.connect(self._on_save_config)
        file_menu.addAction(save_action)

        file_menu.addSeparator()

        exit_action = QAction("退出(&Q)", self)
        exit_action.setShortcut(QKeySequence.Quit)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        view_menu = menubar.addMenu("视图(&V)")

        self._theme_action = QAction("深色模式", self)
        self._theme_action.setCheckable(True)
        self._theme_action.setChecked(False)
        self._theme_action.setShortcut("Ctrl+D")
        self._theme_action.toggled.connect(self._toggle_theme)
        view_menu.addAction(self._theme_action)

        view_menu.addSeparator()

        reset_action = QAction("重置布局", self)
        reset_action.triggered.connect(self._reset_layout)
        view_menu.addAction(reset_action)

        help_menu = menubar.addMenu("帮助(&H)")

        about_action = QAction("关于", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)

    # ── UI Layout ──

    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(6, 6, 6, 6)
        main_layout.setSpacing(4)

        self._main_splitter = QSplitter(Qt.Orientation.Horizontal)

        self._param_panel = ParamPanel()
        self._param_panel.setMinimumWidth(360)
        self._param_panel.setMaximumWidth(500)
        self._param_panel.runRequested.connect(self._on_run)
        self._param_panel.stopRequested.connect(self._on_stop)
        self._param_panel.clearRequested.connect(self._on_clear)
        self._param_panel.restoreDefaultsRequested.connect(self._on_restore_defaults)

        self._right_splitter = QSplitter(Qt.Orientation.Vertical)

        self._plot_panel = PlotPanel()
        self._console_panel = ConsolePanel()

        self._right_splitter.addWidget(self._plot_panel)
        self._right_splitter.addWidget(self._console_panel)
        self._right_splitter.setStretchFactor(0, 3)
        self._right_splitter.setStretchFactor(1, 1)
        self._right_splitter.setSizes([540, 360])

        self._main_splitter.addWidget(self._param_panel)
        self._main_splitter.addWidget(self._right_splitter)
        self._main_splitter.setStretchFactor(0, 0)
        self._main_splitter.setStretchFactor(1, 1)
        self._main_splitter.setSizes([408, 1092])

        main_layout.addWidget(self._main_splitter)

    # ── Status Bar ──

    def _setup_statusbar(self):
        self._status_bar = QStatusBar()
        self.setStatusBar(self._status_bar)

        self._status_mode = QLabel("就绪")
        self._status_mode.setStyleSheet("font-weight: bold;")
        self._status_bar.addWidget(self._status_mode)

        self._status_bar.addPermanentWidget(QLabel("  "))

        self._progress_bar = QProgressBar()
        self._progress_bar.setFixedWidth(200)
        self._progress_bar.setFixedHeight(18)
        self._progress_bar.setRange(0, 100)
        self._progress_bar.setValue(0)
        self._progress_bar.setVisible(False)
        self._status_bar.addPermanentWidget(self._progress_bar)

        self._status_info = QLabel("")
        self._status_bar.addPermanentWidget(self._status_info)

        self._status_time = QLabel("")
        self._status_bar.addPermanentWidget(self._status_time)
        self._update_clock()

    def _update_clock(self):
        from datetime import datetime
        self._status_time.setText(datetime.now().strftime("%H:%M:%S"))

    # ── Config Management ──

    def _auto_load_config(self):
        config_home = self._get_config_home()
        config_path = os.path.join(config_home, "config.json")

        if os.path.isfile(config_path):
            self._load_config(config_path)
            return

        found = self._search_config()
        if found:
            os.makedirs(config_home, exist_ok=True)
            shutil.copy2(found, config_path)
            self._console_panel.append(f"[配置] 已从 {found} 复制到 {config_path}", "info")
            self._load_config(config_path)
            return

        self._console_panel.append("[配置] 未找到 config.json，使用默认参数。", "warning")

    @staticmethod
    def _is_frozen_app():
        return getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS')

    def _get_config_home(self):
        if self._is_frozen_app():
            exe_dir = os.path.dirname(sys.executable)
            if sys.platform == 'darwin':
                return os.path.dirname(os.path.dirname(os.path.dirname(exe_dir)))
            return os.path.dirname(os.path.dirname(exe_dir))
        return os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[0])))

    def _search_config(self):
        env_path = os.environ.get("SPS_CONFIG", "")
        if env_path and os.path.isfile(env_path):
            return env_path

        if self._is_frozen_app():
            exe_dir = os.path.dirname(sys.executable)
            if sys.platform == 'darwin':
                start_dir = os.path.dirname(os.path.dirname(os.path.dirname(exe_dir)))
            else:
                start_dir = os.path.dirname(os.path.dirname(exe_dir))
        else:
            start_dir = os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[0])))

        d = start_dir
        for _ in range(10):
            candidate = os.path.join(d, "config.json")
            if os.path.isfile(candidate):
                return candidate
            parent = os.path.dirname(d)
            if parent == d:
                break
            d = parent

        user_cfg = os.path.expanduser("~/.config/sps/config.json")
        if os.path.isfile(user_cfg):
            return user_cfg

        return None

    def _load_config(self, path):
        ok = self._config.load(path)
        if ok:
            self._console_panel.append(f"[配置] 已加载: {path}", "success")
            self._apply_config_to_panel()
        else:
            self._console_panel.append(f"[配置] 加载失败: {path}", "error")

    def _apply_config_to_panel(self):
        flat = {}
        self._flatten_dict(self._config.get_all_params(), "", flat)
        self._param_panel.set_params(flat)

    def _flatten_dict(self, d, prefix, result):
        for k, v in d.items():
            key = f"{prefix}.{k}" if prefix else k
            if isinstance(v, dict):
                self._flatten_dict(v, key, result)
            elif v is not None:
                result[key] = v

    def _on_load_config(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "加载配置文件", "", "JSON 文件 (*.json);;所有文件 (*)"
        )
        if path:
            self._load_config(path)

    def _on_save_config(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "保存配置文件", "config.json", "JSON 文件 (*.json)"
        )
        if path:
            params = self._param_panel.get_all_params()
            nested = {}
            for key, val in params.items():
                parts = key.split(".")
                obj = nested
                for p in parts[:-1]:
                    if p not in obj:
                        obj[p] = {}
                    obj = obj[p]
                obj[parts[-1]] = val
            try:
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(nested, f, indent=4, ensure_ascii=False)
                self._console_panel.append(f"[配置] 已保存: {path}", "success")
            except Exception as e:
                self._console_panel.append(f"[配置] 保存失败: {e}", "error")

    # ── Computation ──

    def _on_run(self, modes):
        if self._compute_thread and self._compute_thread.isRunning():
            return

        if not modes:
            return

        config_dict = self._param_panel.get_all_params()
        for key, val in config_dict.items():
            self._config.set_param(key, val)

        derived = self._config.get_derived_params()

        errors, warnings = self._validate_params(derived)
        for w in warnings:
            self._console_panel.append(w, "warning")
        if errors:
            for err in errors:
                self._console_panel.append(err, "error")
            return

        self._param_panel.set_running(True)
        self._status_mode.setText("计算中...")
        self._status_info.setText(f"共 {len(modes)} 个任务")
        self._progress_bar.setVisible(True)
        self._progress_bar.setValue(0)

        if self._compute_thread is not None:
            try:
                self._compute_thread.logSignal.disconnect(self._on_log)
                self._compute_thread.resultSignal.disconnect(self._on_case_finished)
                self._compute_thread.allFinishedSignal.disconnect(self._on_all_finished)
                self._compute_thread.errorSignal.disconnect(self._on_compute_error)
            except RuntimeError:
                pass

        self._total_cases = len(modes)
        self._completed_cases = 0
        self._compute_thread = ComputeThread(modes, config_dict)
        self._compute_thread.logSignal.connect(self._on_log)
        self._compute_thread.resultSignal.connect(self._on_case_finished)
        self._compute_thread.allFinishedSignal.connect(self._on_all_finished)
        self._compute_thread.errorSignal.connect(self._on_compute_error)

        self._compute_thread.start()

    def _on_stop(self):
        if self._compute_thread and self._compute_thread.isRunning():
            self._compute_thread.terminate()
            self._console_panel.append("正在停止计算...", "warning")

    def _validate_params(self, derived):
        errors = []
        warnings = []
        Tp  = derived.get("Tp", 12e-6)
        B   = derived.get("B", 40e6)
        prf = derived.get("prf", 10e3)
        fs  = derived.get("fs", 120e6)

        if Tp <= 0:
            errors.append("错误: 脉冲宽度 Tp 必须大于 0")
        if B <= 0:
            errors.append("错误: 信号带宽 B 必须大于 0")
        if prf <= 0:
            errors.append("错误: 脉冲重复频率 prf 必须大于 0")

        if fs > 0 and B > 0 and fs < 2 * B:
            warnings.append(
                f"警告: 采样频率 fs={fs:.2e} Hz 低于奈奎斯特频率 2B={2*B:.2e} Hz"
            )

        return errors, warnings

    def _on_clear(self):
        self._plot_panel.clear_plots()
        self._console_panel.append("绘图已清除。", "dim")

    def _on_restore_defaults(self):
        self._plot_panel.clear_plots()
        self._apply_config_to_panel()
        self._console_panel.append("[参数] 已恢复为配置文件默认值。", "success")

    def _on_log(self, text, level="info"):
        self._console_panel.append(text, level)

    def _on_case_finished(self, func_type, arg, result):
        self._last_result = result
        self._last_func_type = func_type
        self._last_arg = arg

        # 注入 fs 和 fc 到 result (供 plot_panel 使用)
        derived = self._config.get_derived_params()
        result["_fs"] = derived.get("fs", 120e6)
        result["_fc"] = derived.get("fc", 16e9)

        # 更新绘图
        self._plot_panel.update_result(func_type, arg, result)

        # 进度
        self._completed_cases += 1
        progress = int(self._completed_cases / self._total_cases * 100)
        self._progress_bar.setValue(progress)

        # 状态栏摘要
        elapsed = result.get("elapsed", 0.0)
        if func_type == "processing_rd":
            nrn = result.get("nrn", 0)
            nan1 = result.get("nan1", 0)
            self._status_info.setText(
                f"[{self._completed_cases}/{self._total_cases}] "
                f"Case {arg}: RD map {nrn}x{nan1} | 耗时={elapsed*1000:.1f}ms"
            )
        elif func_type == "processing_decouple":
            jsr = result.get("jsr_dB", 0.0)
            gj_count = result.get("gaojiepu_count", 0)
            nan1 = result.get("nan1", 0)
            jam_name = _JAMMING_TYPE_NAMES.get(arg, f"Type {arg}")
            self._status_info.setText(
                f"[{self._completed_cases}/{self._total_cases}] "
                f"Case 6: JSR干扰抑制比={jsr:.1f}dB | "
                f"高阶谱={gj_count}/{nan1} | "
                f"干扰: {jam_name} | 耗时={elapsed*1000:.1f}ms"
            )

    def _on_all_finished(self):
        self._param_panel.set_running(False)
        self._status_mode.setText("就绪")
        self._progress_bar.setVisible(False)
        self._progress_bar.setValue(0)

    def _on_compute_error(self, error_msg):
        self._param_panel.set_running(False)
        self._status_mode.setText("错误")
        self._progress_bar.setVisible(False)
        self._progress_bar.setValue(0)
        self._console_panel.append(f"错误: {error_msg}", "error")

    def _reset_layout(self):
        self._main_splitter.setSizes([408, 1092])
        self._right_splitter.setSizes([540, 360])

    def _show_about(self):
        theme_label = "深色" if self._current_theme == "dark" else "浅色"
        QMessageBox.about(
            self,
            "关于",
            "雷达信号处理系统\n"
            "模块04: 信号处理\n\n"
            "功能:\n"
            "  - 距离-多普勒处理 (Cases 1-5)\n"
            "  - 时频干扰解耦 (Case 6)\n\n"
            "干扰类型: ISDJ / ISRJ / ISCJ / NBJ / RDJ\n\n"
            f"当前主题: {theme_label}\n"
            "技术栈: PySide6 + pyqtgraph + numpy/scipy\n\n"
            "作者: XDU_LJH",
        )

    def _apply_win32_taskbar_icon(self):
        if sys.platform != 'win32':
            return
        try:
            import ctypes
            from ctypes import c_void_p, c_int, c_uint, c_wchar_p

            user32 = ctypes.windll.user32

            user32.LoadImageW.restype = c_void_p
            user32.LoadImageW.argtypes = [c_void_p, c_wchar_p, c_uint, c_int, c_int, c_uint]
            user32.SendMessageW.restype = c_void_p
            user32.SendMessageW.argtypes = [c_void_p, c_uint, c_void_p, c_void_p]
            user32.SetWindowPos.restype = c_int
            user32.SetWindowPos.argtypes = [c_void_p, c_void_p, c_int, c_int, c_int, c_int, c_uint]

            has_class_long = False
            try:
                user32.SetClassLongPtrW.restype = c_void_p
                user32.SetClassLongPtrW.argtypes = [c_void_p, c_int, c_void_p]
                has_class_long = True
            except (AttributeError, OSError):
                pass

            hwnd = int(self.winId())
            ico_path = os.path.join(_assets_dir(), 'app_icon.ico')
            if not os.path.exists(ico_path):
                return
            win_path = os.path.abspath(ico_path).replace('/', '\\')

            hicon_small = user32.LoadImageW(None, win_path, 1, 16, 16, 0x10)
            hicon_big = user32.LoadImageW(None, win_path, 1, 32, 32, 0x10)

            if hicon_small:
                if has_class_long:
                    user32.SetClassLongPtrW(hwnd, -34, hicon_small)
                user32.SendMessageW(hwnd, 0x0080, 0, hicon_small)

            if hicon_big:
                if has_class_long:
                    user32.SetClassLongPtrW(hwnd, -14, hicon_big)
                user32.SendMessageW(hwnd, 0x0080, 1, hicon_big)

            user32.SetWindowPos(hwnd, 0, 0, 0, 0, 0, 0x0002 | 0x0001 | 0x0004 | 0x0020)

        except Exception:
            pass

    def showEvent(self, event):
        super().showEvent(event)
        if not self._win32_icon_applied:
            self._win32_icon_applied = True
            from PySide6.QtCore import QTimer
            QTimer.singleShot(200, self._apply_win32_taskbar_icon)

    def closeEvent(self, event):
        if self._compute_thread and self._compute_thread.isRunning():
            self._compute_thread.terminate()
            self._compute_thread.wait(3000)
        event.accept()
