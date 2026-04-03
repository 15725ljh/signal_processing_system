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
from PySide6.QtCore import Qt, QThread, Signal, QTimer, QSettings
from PySide6.QtGui import QAction, QKeySequence, QFont, QIcon

from ui.theme import LIGHT_STYLE, DARK_STYLE, _assets_dir
from ui.param_panel import ParamPanel
from ui.plot_panel import PlotPanel, apply_plot_theme
from ui.console_panel import ConsolePanel
from core.config_manager import ConfigManager

import waveform_cpp


def _run_waveform_cpp(mode, params):
    """Thin wrapper: flat params dict → waveform_cpp.run_waveform()"""
    system_cfg = {
        "fc":   params.get("system.fc", 16e9),
        "Tp":   params.get("system.Tp", 12e-6),
        "B":    params.get("system.B", 40e6),
        "prf":  params.get("system.prf", 10e3),
        "Vr":   params.get("system.Vr", 50.0),
        "Rs":   params.get("system.Rs", 10000.0),
        "wr":   params.get("system.wr", 608.0),
        "nan1": params.get("system.nan1", 64),
    }
    waveform_cfg = {
        "case1_freq_hop.N":           params.get("waveform.case1_freq_hop.N", 10),
        "case1_freq_hop.delta_f":     params.get("waveform.case1_freq_hop.delta_f", 40e6),
        "case3_pri_jitter.prt":       params.get("waveform.case3_pri_jitter.prt", 1000e-6),
        "case3_pri_jitter.amp":       params.get("waveform.case3_pri_jitter.amp", 1.0),
        "case3_pri_jitter.jitter_us": params.get("waveform.case3_pri_jitter.jitter_us", 20),
        "case4_hybrid.delta_f":       params.get("waveform.case4_hybrid.delta_f", 40e6),
        "case4_hybrid.fcnum":         params.get("waveform.case4_hybrid.fcnum", 16),
        "case4_hybrid.amp":           params.get("waveform.case4_hybrid.amp", 1.0),
        "case4_hybrid.prt":           params.get("waveform.case4_hybrid.prt", 1000e-6),
        "case4_hybrid.jitter_us":     params.get("waveform.case4_hybrid.jitter_us", 20),
        "case5_combined.N":           params.get("waveform.case5_combined.N", 10),
        "case5_combined.delta_f":     params.get("waveform.case5_combined.delta_f", 40e6),
    }
    result = waveform_cpp.run_waveform(mode, system_cfg, waveform_cfg)
    return {
        "radar_sig": result["radar_sig"],
        "f": result.get("f", np.zeros(64)),
        "phi1": result.get("phi1", np.zeros(64, dtype=complex)),
        "freq_seq": result.get("freq_seq", np.zeros(64)),
        "has_freq_hop": result.get("has_freq_hop", False),
        "has_random_phase": result.get("has_random_phase", False),
        "has_freq_seq": result.get("has_freq_seq", False),
    }


class ComputeThread(QThread):
    logSignal = Signal(str, str)
    progressSignal = Signal(int, int)
    finishedSignal = Signal(object)
    errorSignal = Signal(str)

    def __init__(self, params, modes, parent=None):
        super().__init__(parent)
        self._params = params
        self._modes = modes
        self._stop_flag = False

    def run(self):
        try:
            mode_names = {
                1: "固定跳频波形",
                2: "随机相位波形",
                3: "脉冲重复间隔抖动波形",
                4: "混合波形(跳频+抖动)",
                5: "跳频+随机相位复合波形",
            }

            results = {}
            total = len(self._modes)
            for idx, mode in enumerate(self._modes):
                if self._stop_flag:
                    self.logSignal.emit("计算已被用户中止。", "warning")
                    break

                self.logSignal.emit(f"开始计算 模式{mode}: {mode_names.get(mode, '')}", "header")
                self.progressSignal.emit(idx, total)

                t0 = time.time()
                result = _run_waveform_cpp(mode, self._params)
                elapsed = time.time() - t0

                if result.get("log_output"):
                    for line in result["log_output"].split("\n"):
                        if line.strip():
                            self.logSignal.emit(line, "info")

                results[mode] = result

                if result["radar_sig"] is not None:
                    r = min(360, result["radar_sig"].shape[0] - 1)
                    c_idx = min(32, result["radar_sig"].shape[1] - 1)
                    self.logSignal.emit(
                        f"Radar_Sig({r},{c_idx}) = {result['radar_sig'][r, c_idx]}", "info"
                    )

                self.logSignal.emit(f"模式{mode} 完成 (耗时 {elapsed*1000:.1f} ms)", "success")

            self.progressSignal.emit(total, total)
            self.finishedSignal.emit(results)

        except Exception as e:
            self.errorSignal.emit(str(e))

    def stop(self):
        self._stop_flag = True


class MainWindow(QMainWindow):

    def __init__(self, config_path=None):
        super().__init__()
        self.setWindowTitle("雷达波形生成系统 - 模块01")
        icon_path = os.path.join(_assets_dir(), 'app_icon.ico')
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))
        self.setMinimumSize(1280, 800)
        self._win32_icon_applied = False
        self.resize(1500, 920)

        self._config = ConfigManager()
        self._compute_thread = None
        self._last_results = None
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
        """Bug 7 fix: switch between light and dark themes."""
        theme = "dark" if dark else "light"
        self._current_theme = theme
        style = DARK_STYLE if dark else LIGHT_STYLE
        self.setStyleSheet(style)

        # Update plot colors
        apply_plot_theme(theme)

        # Update console colors
        self._console_panel.apply_theme(theme)

        # Update derived labels in param panel
        for lbl in self._param_panel._derived_labels.values():
            lbl.apply_theme(theme)

        # Refresh plots if data exists
        if self._last_results:
            modes = sorted(self._last_results.keys())
            if modes:
                self._plot_panel.update_plots(self._last_results, default_mode=modes[-1])

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
        self._param_panel.setMaximumWidth(480)
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
        self._right_splitter.setSizes([540, 280])

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

        # Bug 6 fix: add actual progress bar widget
        self._progress_bar = QProgressBar()
        self._progress_bar.setFixedWidth(200)
        self._progress_bar.setFixedHeight(18)
        self._progress_bar.setTextVisible(True)
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
            exe = os.path.dirname(os.path.dirname(os.path.dirname(sys.executable)))
            return os.path.dirname(exe)
        return os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[0])))

    def _search_config(self):
        env_path = os.environ.get("SPS_CONFIG", "")
        if env_path and os.path.isfile(env_path):
            return env_path

        if self._is_frozen_app():
            start_dir = os.path.dirname(os.path.dirname(os.path.dirname(sys.executable)))
            start_dir = os.path.dirname(start_dir)
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
        # Resolve null (meaning "use system B") for delta_f fields
        B = flat.get("system.B", 40e6)
        for null_key in [
            "waveform.case1_freq_hop.delta_f",
            "waveform.case4_hybrid.delta_f",
            "waveform.case5_combined.delta_f",
        ]:
            if null_key not in flat:
                flat[null_key] = B
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

        params = self._param_panel.get_all_params()
        for key, val in params.items():
            self._config.set_param(key, val)

        derived = self._config.get_derived_params()

        errors = self._validate_params(params, derived)
        if errors:
            for err in errors:
                self._console_panel.append(err, "error")
            return

        self._plot_panel.set_time_freq_axes(derived["tnrn"], derived["fr"], derived["fc"])

        self._param_panel.set_running(True)
        self._status_mode.setText("计算中...")
        self._status_info.setText(f"模式: {modes}")
        self._progress_bar.setVisible(True)
        self._progress_bar.setValue(0)

        if self._compute_thread is not None:
            try:
                self._compute_thread.logSignal.disconnect(self._on_log)
                self._compute_thread.progressSignal.disconnect(self._on_progress)
                self._compute_thread.finishedSignal.disconnect(self._on_compute_finished)
                self._compute_thread.errorSignal.disconnect(self._on_compute_error)
            except RuntimeError:
                pass

        self._compute_thread = ComputeThread(params, modes)
        self._compute_thread.logSignal.connect(self._on_log)
        self._compute_thread.progressSignal.connect(self._on_progress)
        self._compute_thread.finishedSignal.connect(self._on_compute_finished)
        self._compute_thread.errorSignal.connect(self._on_compute_error)

        self._compute_thread.start()

    def _validate_params(self, params, derived):
        errors = []
        Tp = derived["Tp"]
        B = derived["B"]
        prf = derived["prf"]
        fs = derived["fs"]
        nan1 = derived["nan1"]
        prt = derived["prt"]

        if Tp <= 0:
            errors.append("错误: 脉冲宽度 Tp 必须大于 0")
        if B <= 0:
            errors.append("错误: 信号带宽 B 必须大于 0")
        if prf <= 0:
            errors.append("错误: 脉冲重复频率 prf 必须大于 0")

        if fs > 0 and B > 0 and fs < 2 * B:
            errors.append(f"警告: 采样频率 fs={fs:.2e} Hz 低于奈奎斯特频率 2B={2*B:.2e} Hz，结果可能混叠")

        fcnum = params.get("waveform.case4_hybrid.fcnum", 16)
        if fcnum > 0 and nan1 % fcnum != 0:
            errors.append(f"错误: 方位向脉冲数 nan1={nan1} 必须能被载频分组数 fcnum={fcnum} 整除")

        jitter_us = params.get("waveform.case3_pri_jitter.jitter_us", 20)
        jitter_s = jitter_us * 1e-6
        if prt > 0 and jitter_s >= prt:
            errors.append(f"错误: 抖动范围 ±{jitter_us}μs 超过了脉冲间隔 prt={prt*1e6:.1f}μs")

        # Bug 4 fix: validate Case4 prt positivity explicitly
        prt_4 = params.get("waveform.case4_hybrid.prt", 1000e-6)
        if prt_4 <= 0:
            errors.append("错误: Case4 脉冲间隔 prt 必须大于 0")
        else:
            jitter_us_4 = params.get("waveform.case4_hybrid.jitter_us", 20)
            jitter_s_4 = jitter_us_4 * 1e-6
            if jitter_s_4 >= prt_4:
                errors.append(f"错误: Case4 抖动范围 ±{jitter_us_4}μs 超过了脉冲间隔 prt={prt_4*1e6:.1f}μs")

        return errors

    def _on_stop(self):
        if self._compute_thread and self._compute_thread.isRunning():
            self._compute_thread.stop()
            self._console_panel.append("正在停止计算...", "warning")

    def _on_clear(self):
        self._plot_panel.clear_plots()
        self._console_panel.append("绘图已清除。", "dim")

    def _on_restore_defaults(self):
        self._plot_panel.clear_plots()
        # Re-apply config.json values instead of hardcoded defaults
        self._apply_config_to_panel()
        for cb in self._param_panel._case_checks.values():
            cb.setChecked(True)
        self._console_panel.append("[参数] 已恢复为配置文件默认值。", "success")

    def _on_log(self, text, level="info"):
        self._console_panel.append(text, level)

    def _on_progress(self, current, total):
        if total > 0:
            pct = int(current / total * 100)
            self._progress_bar.setVisible(True)
            self._progress_bar.setValue(pct)
            self._status_info.setText(f"进度: {pct}% ({current}/{total})")

    def _on_compute_finished(self, results):
        self._last_results = results
        self._param_panel.set_running(False)
        self._status_mode.setText("就绪")
        self._progress_bar.setVisible(False)
        self._progress_bar.setValue(0)

        if results:
            last_mode = list(results.keys())[-1]
            self._plot_panel.update_plots(results, default_mode=last_mode)
            self._status_info.setText(f"已完成 {len(results)} 个模式")

    def _on_compute_error(self, error_msg):
        self._param_panel.set_running(False)
        self._status_mode.setText("错误")
        self._progress_bar.setVisible(False)
        self._progress_bar.setValue(0)
        self._console_panel.append(f"错误: {error_msg}", "error")

    def _reset_layout(self):
        self._main_splitter.setSizes([408, 1092])
        self._right_splitter.setSizes([540, 280])

    def _show_about(self):
        theme_label = "深色" if self._current_theme == "dark" else "浅色"
        QMessageBox.about(
            self,
            "关于",
            "雷达波形生成系统\n"
            "模块01: 波形生成\n\n"
            "支持 5 种波形模式 (Case 1~5)\n"
            "固定跳频 / 随机相位 / PRI抖动\n"
            "混合波形 / 复合波形\n\n"
            f"当前主题: {theme_label}\n"
            "技术栈: PySide6 + pyqtgraph + numpy/scipy\n\n"
            "作者: XDU_LJH",
        )

    def _apply_win32_taskbar_icon(self):
        """通过 Win32 API 设置窗口图标, 确保 Windows 11 任务栏正确显示。
        同时设置窗口类图标 (SetClassLongPtrW) 和窗口图标 (WM_SETICON),
        类图标比 WM_SETICON 更持久, 不易被 Qt 内部调用覆盖。"""
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
            # 延迟执行: Qt 在 showEvent 之后可能内部重置图标,
            # 用 QTimer.singleShot 确保 Win32 调用在 Qt 处理完毕后执行
            from PySide6.QtCore import QTimer
            QTimer.singleShot(200, self._apply_win32_taskbar_icon)

    def closeEvent(self, event):
        if self._compute_thread and self._compute_thread.isRunning():
            self._compute_thread.stop()
            self._compute_thread.wait(3000)
        event.accept()
