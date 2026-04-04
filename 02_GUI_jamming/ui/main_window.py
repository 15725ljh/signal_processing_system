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

import jamming_cpp


def _run_jamming_cpp(mode, params):
    """Thin wrapper: flat params dict -> jamming_cpp.run_jamming()"""
    system_cfg = {
        "fc":   params.get("system.fc", 16e9),
        "Tp":   params.get("system.Tp", 12e-6),
        "B":    params.get("system.B", 40e6),
        "prf":  params.get("system.prf", 10e3),
        "Vr":   params.get("system.Vr", 50.0),
        "Rs":   params.get("system.Rs", 10000.0),
        "wr":   params.get("system.wr", 608.0),
        "A_RJ": params.get("system.A_RJ", 10.0),
        "z_R0": params.get("system.z_R0", 2000.0),
        "nan1": params.get("system.nan1", 64),
    }
    jamming_cfg = {
        # Case 1
        "case1_rdj.jj":         params.get("jamming.case1_rdj.jj", 1),
        "case1_rdj.Rj":         params.get("jamming.case1_rdj.Rj", 100.0),
        "case1_rdj.amp_j":      params.get("jamming.case1_rdj.amp_j", 10.0),
        "case1_rdj.amp_target": params.get("jamming.case1_rdj.amp_target", 1.0),
        "case1_rdj.awgn_snr":   params.get("jamming.case1_rdj.awgn_snr", 10.0),
        # Case 2
        "case2_vdj.Vj":         params.get("jamming.case2_vdj.Vj", 1e5),
        "case2_vdj.jj":         params.get("jamming.case2_vdj.jj", 1),
        "case2_vdj.Rj":         params.get("jamming.case2_vdj.Rj", 10.0),
        "case2_vdj.amp_j":      params.get("jamming.case2_vdj.amp_j", 10.0),
        "case2_vdj.amp_target": params.get("jamming.case2_vdj.amp_target", 1.0),
        "case2_vdj.awgn_snr":   params.get("jamming.case2_vdj.awgn_snr", 10.0),
        # Case 3
        "case3_isrj.Ts_ISRJ":    params.get("jamming.case3_isrj.Ts_ISRJ", 4e-6),
        "case3_isrj.T_ISRJ":     params.get("jamming.case3_isrj.T_ISRJ", 0.0),
        "case3_isrj.Rj":         params.get("jamming.case3_isrj.Rj", 10.0),
        "case3_isrj.amp_j":      params.get("jamming.case3_isrj.amp_j", 10.0),
        "case3_isrj.amp_target": params.get("jamming.case3_isrj.amp_target", 1.0),
        "case3_isrj.awgn_snr":   params.get("jamming.case3_isrj.awgn_snr", 10.0),
        # Case 4
        "case4_nnj.power_dBW":     params.get("jamming.case4_nnj.power_dBW", 20.0),
        "case4_nnj.butter_order":  params.get("jamming.case4_nnj.butter_order", 8),
        "case4_nnj.butter_cutoff": params.get("jamming.case4_nnj.butter_cutoff", 0.3),
        "case4_nnj.amp_target":    params.get("jamming.case4_nnj.amp_target", 1.0),
        "case4_nnj.awgn_snr":      params.get("jamming.case4_nnj.awgn_snr", 10.0),
        # Case 5
        "case5_rgpo.Vj":          params.get("jamming.case5_rgpo.Vj", 340.0),
        "case5_rgpo.amp_target":  params.get("jamming.case5_rgpo.amp_target", 1.0),
        "case5_rgpo.amp_jammer":  params.get("jamming.case5_rgpo.amp_jammer", 1.4),
        "case5_rgpo.drag_stages": params.get("jamming.case5_rgpo.drag_stages", 4),
        "case5_rgpo.awgn_snr":    params.get("jamming.case5_rgpo.awgn_snr", 10.0),
        # Case 6
        "case6_vgpo.Vj":          params.get("jamming.case6_vgpo.Vj", 1e4),
        "case6_vgpo.amp_target":  params.get("jamming.case6_vgpo.amp_target", 1.0),
        "case6_vgpo.amp_jammer":  params.get("jamming.case6_vgpo.amp_jammer", 1.4),
        "case6_vgpo.drag_stages": params.get("jamming.case6_vgpo.drag_stages", 4),
        "case6_vgpo.awgn_snr":    params.get("jamming.case6_vgpo.awgn_snr", 10.0),
        # Case 7
        "case7_drftj.JSR":        params.get("jamming.case7_drftj.JSR", 0.0),
        "case7_drftj.amp_target": params.get("jamming.case7_drftj.amp_target", 1.0),
        "case7_drftj.num_jam":    params.get("jamming.case7_drftj.num_jam", 50),
        "case7_drftj.detaR":      params.get("jamming.case7_drftj.detaR", 50.0),
        "case7_drftj.awgn_snr":   params.get("jamming.case7_drftj.awgn_snr", 10.0),
        # Case 8
        "case8_iplesrj.A_RJ":         params.get("jamming.case8_iplesrj.A_RJ", 30.0),
        "case8_iplesrj.amp_target":   params.get("jamming.case8_iplesrj.amp_target", 1.0),
        "case8_iplesrj.R0_new":       params.get("jamming.case8_iplesrj.R0_new", 608.0),
        "case8_iplesrj.V_ISRJ":       params.get("jamming.case8_iplesrj.V_ISRJ", 0.0),
        "case8_iplesrj.T_ISRJ_ratio": params.get("jamming.case8_iplesrj.T_ISRJ_ratio", 16),
        "case8_iplesrj.R_ahead":      params.get("jamming.case8_iplesrj.R_ahead", 0.0),
        "case8_iplesrj.awgn_snr":     params.get("jamming.case8_iplesrj.awgn_snr", 10.0),
        # Case 9
        "case9_smsp.num_slices": params.get("jamming.case9_smsp.num_slices", 4),
        "case9_smsp.JSR":        params.get("jamming.case9_smsp.JSR", 15.0),
        "case9_smsp.amp_target": params.get("jamming.case9_smsp.amp_target", 1.0),
        "case9_smsp.amp_extra":  params.get("jamming.case9_smsp.amp_extra", 1.4),
        "case9_smsp.R0":         params.get("jamming.case9_smsp.R0", 10000.0),
        "case9_smsp.awgn_snr":   params.get("jamming.case9_smsp.awgn_snr", 10.0),
        # Case 10
        "case10_comb.num_tones":  params.get("jamming.case10_comb.num_tones", 7),
        "case10_comb.JSR":        params.get("jamming.case10_comb.JSR", 0.0),
        "case10_comb.amp_target": params.get("jamming.case10_comb.amp_target", 1.0),
        "case10_comb.deltaf":     params.get("jamming.case10_comb.deltaf", 1e6),
        "case10_comb.R0":         params.get("jamming.case10_comb.R0", 10000.0),
        "case10_comb.awgn_snr":   params.get("jamming.case10_comb.awgn_snr", 10.0),
    }
    result = jamming_cpp.run_jamming(mode, system_cfg, jamming_cfg)
    return {
        "echo_target":   result["echo_target"],
        "jam_signal":    result["jam_signal"],
        "target_signal": result["target_signal"],
        "nrn":  result["nrn"],
        "nan1": result["nan1"],
        "log_output":    result.get("log_output", ""),
    }


class ComputeThread(QThread):
    logSignal = Signal(str, str)
    progressSignal = Signal(int, int)
    finishedSignal = Signal(object)  # dict: mode -> result
    errorSignal = Signal(str)

    def __init__(self, params, modes, parent=None):
        super().__init__(parent)
        self._params = params
        self._modes = modes
        self._stop_flag = False

    def run(self):
        try:
            mode_names = {
                1: "距离假目标 (RDJ)",
                2: "速度假目标 (VDJ)",
                3: "间歇采样转发 (ISRJ)",
                4: "窄带噪声 (NNJ)",
                5: "距离波门拖引 (RGPO)",
                6: "速度波门拖引 (VGPO)",
                7: "密集假目标 (DRFTJ)",
                8: "脉内前沿切片 (IPLESRJ)",
                9: "频谱弥散 (SMSP)",
                10: "梳状谱 (COMB)",
            }

            total = len(self._modes)
            results = {}
            for idx, mode in enumerate(self._modes):
                if self._stop_flag:
                    self.logSignal.emit("计算已被用户中止。", "warning")
                    break

                self.logSignal.emit(f"开始计算 模式{mode}: {mode_names.get(mode, '')}", "header")
                self.progressSignal.emit(idx, total)

                t0 = time.time()
                result = _run_jamming_cpp(mode, self._params)
                elapsed = time.time() - t0

                if result.get("log_output"):
                    for line in result["log_output"].split("\n"):
                        if line.strip():
                            self.logSignal.emit(line, "info")

                echo = result.get("echo_target")
                if echo is not None:
                    nrn, nan1 = echo.shape
                    self.logSignal.emit(
                        f"echo_target shape: ({nrn}, {nan1})", "info"
                    )

                self.logSignal.emit(f"模式{mode} 完成 (耗时 {elapsed*1000:.1f} ms)", "success")

                results[mode] = result
                self.progressSignal.emit(idx + 1, total)

            self.finishedSignal.emit(results)

        except Exception as e:
            self.errorSignal.emit(str(e))

    def stop(self):
        self._stop_flag = True


class MainWindow(QMainWindow):

    def __init__(self, config_path=None):
        super().__init__()
        self.setWindowTitle("雷达干扰生成系统 - 模块02")
        icon_path = os.path.join(_assets_dir(), 'app_icon.ico')
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))
        self.setMinimumSize(1280, 800)
        self._win32_icon_applied = False
        self.resize(1500, 920)

        self._config = ConfigManager()
        self._compute_thread = None
        self._last_result = None
        self._last_mode = None
        self._last_results = {}
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
        if self._last_results:
            modes = sorted(self._last_results.keys())
            if modes:
                self._plot_panel.update_plots(modes[-1], self._last_results[modes[-1]])

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
        self._main_splitter.setSizes([420, 1080])

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
            exe_dir = os.path.dirname(sys.executable)
            return os.path.dirname(os.path.dirname(exe_dir))
        return os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[0])))

    def _search_config(self):
        env_path = os.environ.get("SPS_CONFIG", "")
        if env_path and os.path.isfile(env_path):
            return env_path

        if self._is_frozen_app():
            exe_dir = os.path.dirname(sys.executable)
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

        params = self._param_panel.get_all_params()
        for key, val in params.items():
            self._config.set_param(key, val)

        derived = self._config.get_derived_params()

        errors = self._validate_params(params, derived)
        if errors:
            for err in errors:
                self._console_panel.append(err, "error")
            return

        self._plot_panel.set_time_freq_axes(derived["tnrn"], derived["fr"], derived["fc"],
                                             fs=derived["fs"], gama=derived["gama"],
                                             prf=derived["prf"])

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

    def _on_stop(self):
        if self._compute_thread and self._compute_thread.isRunning():
            self._compute_thread.stop()
            self._console_panel.append("正在停止计算...", "warning")

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

        return errors

    def _on_clear(self):
        self._plot_panel.clear_plots()
        self._console_panel.append("绘图已清除。", "dim")

    def _on_restore_defaults(self):
        self._plot_panel.clear_plots()
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
            # 将所有结果追加到 plot_panel
            for mode, result in results.items():
                self._plot_panel.update_plots(mode, result)
            last_mode = list(results.keys())[-1]
            self._last_mode = last_mode
            self._last_result = results[last_mode]
            self._status_info.setText(f"已完成 {len(results)} 个模式")

    def _on_compute_error(self, error_msg):
        self._param_panel.set_running(False)
        self._status_mode.setText("错误")
        self._progress_bar.setVisible(False)
        self._progress_bar.setValue(0)
        self._console_panel.append(f"错误: {error_msg}", "error")

    def _reset_layout(self):
        self._main_splitter.setSizes([420, 1080])
        self._right_splitter.setSizes([540, 360])

    def _show_about(self):
        theme_label = "深色" if self._current_theme == "dark" else "浅色"
        QMessageBox.about(
            self,
            "关于",
            "雷达干扰生成系统\n"
            "模块02: 干扰生成\n\n"
            "支持 10 种干扰模式 (Case 1~10)\n"
            "RDJ / VDJ / ISRJ / NNJ / RGPO\n"
            "VGPO / DRFTJ / IPLESRJ / SMSP / COMB\n\n"
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
            self._compute_thread.stop()
            self._compute_thread.wait(3000)
        event.accept()
