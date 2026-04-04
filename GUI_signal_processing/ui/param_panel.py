"""
参数面板 — 信号处理 GUI (模块04)

功能选择器 (CheckBox 多选, 与 01/02/03 风格一致) + 参数分组 + 干扰类型选择 + 派生参数
"""

import math

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QLabel, QPushButton, QScrollArea, QFrame,
    QSizePolicy, QComboBox, QCheckBox, QSpinBox,
)
from PySide6.QtCore import Signal, Qt

from .scientific_spinbox import ScientificSpinBox


class ParamEdit(QWidget):
    valueChanged = Signal(str, object)

    def __init__(self, label, key, default, is_int=False, suffix="", step=None, parent=None):
        super().__init__(parent)
        self._key = key
        self._is_int = is_int

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 2, 0, 2)
        layout.setSpacing(6)

        lbl = QLabel(label)
        lbl.setMinimumWidth(140)
        lbl.setWordWrap(True)
        lbl.setToolTip(f"配置项: {key}")
        layout.addWidget(lbl)

        if is_int:
            self._spin = QSpinBox()
            self._spin.setRange(-999999999, 999999999)
            self._spin.setValue(int(default))
            if step is not None:
                self._spin.setSingleStep(int(step))
        else:
            self._spin = ScientificSpinBox()
            self._spin.setValue(float(default))
            if step is not None:
                self._spin.setSingleStep(float(step))

        if suffix:
            self._spin.setSuffix(f" {suffix}")

        self._spin.setMinimumWidth(160)
        self._spin.valueChanged.connect(self._on_change)
        layout.addWidget(self._spin, stretch=1)

    def _on_change(self, val):
        v = int(val) if self._is_int else float(val)
        self.valueChanged.emit(self._key, v)

    def value(self):
        return self._spin.value()

    def set_value(self, val):
        self._spin.blockSignals(True)
        if self._is_int:
            self._spin.setValue(int(val))
        else:
            self._spin.setValue(float(val))
        self._spin.blockSignals(False)


class DerivedLabel(QLabel):
    def __init__(self, text="", parent=None):
        super().__init__(parent)
        self.setText(text)
        self.setStyleSheet(
            "color: #5b8def; font-size: 11px; "
            "padding: 2px 8px; "
            "background: #eef2f7; border-radius: 4px; "
            "font-family: 'Menlo', 'PingFang SC', monospace;"
        )
        self.setAlignment(Qt.AlignmentFlag.AlignRight)

    def set_text(self, text):
        self.setText(text)

    def apply_theme(self, theme="light"):
        from ui.theme import WIDGET_THEMES
        wt = WIDGET_THEMES.get(theme, WIDGET_THEMES["light"])
        self.setStyleSheet(
            f"color: {wt['derived_label_fg']}; font-size: 11px; "
            f"padding: 2px 8px; "
            f"background: {wt['derived_label_bg']}; border-radius: 4px; "
            f"font-family: 'Menlo', 'PingFang SC', monospace;"
        )


# ── 功能模式: CheckBox 多选 (与 01/02/03 风格一致) ──
_CASE_TYPES = {
    1: "Case 1: 跳频信号处理",
    2: "Case 2: 固定载频处理",
    3: "Case 3: 传统脉冲压缩",
    4: "Case 4: 改进型脉冲压缩",
    5: "Case 5: 复合处理",
    6: "Case 6: 时频干扰解耦",
}

# 需要选择干扰类型的 Case
_JAMMING_CASES = {6}

# ── 干扰类型 (ComboBox 单选, 仅 Case 6 使用) ──
_JAMMING_TYPES = {
    1: "ISDJ 间歇采样直接转发",
    2: "ISRJ 间歇采样重复转发",
    3: "ISCJ 间歇采样循环转发",
    4: "NBJ 窄带瞄频噪声",
    5: "RDJ 距离欺骗干扰",
}


class ParamPanel(QWidget):

    paramsChanged = Signal()
    runRequested = Signal(list)         # 多选模式, 传 [(func_type, arg), ...] 列表
    stopRequested = Signal()
    clearRequested = Signal()
    restoreDefaultsRequested = Signal()

    # ── 组1: 系统参数 ──
    _SYSTEM_PARAMS = [
        ("载波频率 fc (Hz)",       "system.fc",           16e9,   False, "", 1e9),
        ("信号带宽 B (Hz)",        "system.B",            40e6,   False, "", 1e6),
        ("脉冲重复频率 prf (Hz)",  "system.prf",          10e3,   False, "", 1e3),
        ("脉冲宽度 Tp (s)",        "system.Tp",           12e-6,  False, "", 1e-6),
        ("相对径向速度 Vr (m/s)",  "system.Vr",           50.0,   False, "", 10),
        ("场景中心斜距 Rs (m)",    "system.Rs",           10000,  False, "", 100),
        ("场景距离向宽度 wr (m)",  "system.wr",           608,    False, "", 10),
        ("干扰幅度增益 A_RJ (dB)", "system.A_RJ",         10,     False, "", 1),
        ("雷达初始高度 z_R0 (m)",  "system.z_R0",         2000,   False, "", 100),
        ("方位向脉冲数 nan1",      "system.nan1",         64,     True,  "", 1),
    ]

    # ── 组2: 解耦参数 (JamTarDivi, Case 6) ──
    _DECOUPLE_PARAMS = [
        ("分离 STFT 频点数",      "detection_suppression.divi_stft_num",      256,  True,  "", 1),
        ("分离汉明窗长度",        "detection_suppression.divi_hamming_len",   31,   True,  "", 1),
        ("Tsallis 熵参数 q",      "detection_suppression.tsallis_q",          2.0,  False, "", 0.1),
        ("高阶谱判定阈值",        "detection_suppression.gaojiepu_threshold", 35,   False, "", 1),
    ]

    # ── 组3: 回波生成器参数 (EchoGenerator4, Case 6) ──
    _ECHO_PARAMS = [
        ("信噪比 SNR (dB)",        "detection_suppression.SNR",          25,   False, "", 1),
        ("干信比 JSR (dB)",        "detection_suppression.JSR",          30,   False, "", 1),
        ("底噪 SNR (dB)",          "detection_suppression.noise_snr",    25,   False, "", 1),
        ("干扰距离下限比例",       "detection_suppression.r_min_ratio",  0.7,  False, "", 0.1),
        ("干扰距离上限比例",       "detection_suppression.r_max_ratio",  1.7,  False, "", 0.1),
    ]

    # ── 组4: 波形参数 (按 Case 分组, 始终可见, 用分隔线区分) ──
    _WAVEFORM_PARAMS = {
        1: [  # Case 1: 跳频
            ("跳频点数 N",          "waveform.case1_freq_hop.N",       10,     True,  "", 1),
            ("频率步进 (Hz)",       "waveform.case1_freq_hop.delta_f", 40e6,   False, "", 1e6),
        ],
        3: [  # Case 3: PRI 抖动
            ("标称 PRT (s)",        "waveform.case3_pri_jitter.prt",       1000e-6, False, "", 10e-6),
            ("信号幅度",            "waveform.case3_pri_jitter.amp",       1.0,     False, "", 0.1),
            ("抖动范围 (us)",       "waveform.case3_pri_jitter.jitter_us", 20,      True,  "", 1),
        ],
        4: [  # Case 4: 混合
            ("频率步进 (Hz)",       "waveform.case4_hybrid.delta_f",   40e6,   False, "", 1e6),
            ("载频分组数",          "waveform.case4_hybrid.fcnum",     16,     True,  "", 1),
            ("标称 PRT (s)",        "waveform.case4_hybrid.prt",       1000e-6, False, "", 10e-6),
            ("抖动范围 (us)",       "waveform.case4_hybrid.jitter_us", 20,     True,  "", 1),
        ],
        5: [  # Case 5: 复合
            ("跳频点数 N",          "waveform.case5_combined.N",       10,     True,  "", 1),
            ("频率步进 (Hz)",       "waveform.case5_combined.delta_f", 40e6,   False, "", 1e6),
        ],
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self._param_edits = {}
        self._locked = False
        self._type_checks = {}         # case_label → QCheckBox
        self._setup_ui()
        self._apply_lock_state()

    def _setup_ui(self):
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(6, 6, 6, 6)
        main_layout.setSpacing(4)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        container = QWidget()
        layout = QVBoxLayout(container)
        layout.setSpacing(6)

        layout.addWidget(self._create_type_select_group())
        layout.addWidget(self._create_action_buttons())
        layout.addWidget(self._create_lock_button())
        layout.addWidget(self._create_system_params_group())
        layout.addWidget(self._create_decouple_params_group())
        layout.addWidget(self._create_echo_params_group())
        layout.addWidget(self._create_waveform_params_group())
        layout.addWidget(self._create_jamming_selector())
        layout.addWidget(self._create_derived_info_group())
        layout.addStretch()

        scroll.setWidget(container)
        main_layout.addWidget(scroll)

    # ── 功能选择 (复选框, 与 01/02/03 一致) ──
    def _create_type_select_group(self):
        group = QGroupBox("功能选择")
        layout = QVBoxLayout(group)

        self._type_checks = {}
        for label, name in _CASE_TYPES.items():
            cb = QCheckBox(name)
            cb.setChecked(True)
            cb.setProperty("case_label", label)
            self._type_checks[label] = cb
            layout.addWidget(cb)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(10)
        select_all = QPushButton("✓ 全选")
        select_all.setObjectName("selectAllBtn")
        select_all.setMinimumWidth(64)
        select_all.clicked.connect(lambda: [c.setChecked(True) for c in self._type_checks.values()])
        deselect_all = QPushButton("✕ 全不选")
        deselect_all.setObjectName("deselectAllBtn")
        deselect_all.setMinimumWidth(64)
        deselect_all.clicked.connect(lambda: [c.setChecked(False) for c in self._type_checks.values()])
        btn_row.addWidget(select_all)
        btn_row.addWidget(deselect_all)
        btn_row.addStretch()
        layout.addLayout(btn_row)

        return group

    # ── 干扰类型选择 (ComboBox 单选, 仅 Case 6 使用) ──
    def _create_jamming_selector(self):
        group = QGroupBox("干扰类型选择 (Case 6)")
        layout = QVBoxLayout(group)
        self._jamming_combo = QComboBox()
        for label, name in _JAMMING_TYPES.items():
            self._jamming_combo.addItem(f"{label}: {name}", userData=label)
        layout.addWidget(self._jamming_combo)
        return group

    # ── 操作按钮 ──
    def _create_action_buttons(self):
        group = QGroupBox("操作")
        layout = QVBoxLayout(group)

        row1 = QHBoxLayout()
        row1.setSpacing(10)

        self._run_btn = QPushButton("运  行")
        self._run_btn.setObjectName("actionRun")
        self._run_btn.clicked.connect(self._on_run)

        self._stop_btn = QPushButton("停  止")
        self._stop_btn.setObjectName("actionStop")
        self._stop_btn.setEnabled(False)
        self._stop_btn.clicked.connect(self.stopRequested.emit)

        row1.addWidget(self._run_btn)
        row1.addWidget(self._stop_btn)
        layout.addLayout(row1)

        row2 = QHBoxLayout()
        row2.setSpacing(10)
        restore_btn = QPushButton("恢复默认")
        restore_btn.setObjectName("actionRestore")
        restore_btn.clicked.connect(self._on_restore_defaults)
        clear_btn = QPushButton("清除绘图")
        clear_btn.setObjectName("actionClear")
        clear_btn.clicked.connect(self.clearRequested.emit)
        row2.addWidget(restore_btn)
        row2.addWidget(clear_btn)
        layout.addLayout(row2)

        return group

    # ── 锁定按钮 ──
    def _create_lock_button(self):
        row = QHBoxLayout()
        row.setSpacing(4)
        self._lock_btn = QPushButton("🔓 解锁参数")
        self._lock_btn.setObjectName("lockToggle")
        self._lock_btn.setCheckable(True)
        self._lock_btn.setChecked(False)
        self._lock_btn.setMinimumHeight(32)
        self._lock_btn.clicked.connect(self._on_lock_toggle)
        row.addWidget(self._lock_btn)
        w = QWidget()
        w.setLayout(row)
        return w

    def _on_lock_toggle(self, checked):
        self._locked = not checked
        self._lock_btn.setText("🔒 参数已锁定" if self._locked else "🔓 解锁参数")
        self._apply_lock_state()

    def _apply_lock_state(self):
        for edit in self._param_edits.values():
            edit._spin.setEnabled(not self._locked)
        for cb in self._type_checks.values():
            cb.setEnabled(not self._locked)
        self._jamming_combo.setEnabled(not self._locked)

    # ── 参数分组 ──
    def _create_system_params_group(self):
        group = QGroupBox("系统参数")
        layout = QVBoxLayout(group)
        for label, key, default, is_int, suffix, step in self._SYSTEM_PARAMS:
            edit = ParamEdit(label, key, default, is_int, suffix, step)
            edit.valueChanged.connect(self._on_param_changed)
            self._param_edits[key] = edit
            layout.addWidget(edit)
        return group

    def _create_decouple_params_group(self):
        group = QGroupBox("解耦参数 (JamTarDivi, Case 6)")
        layout = QVBoxLayout(group)
        for label, key, default, is_int, suffix, step in self._DECOUPLE_PARAMS:
            edit = ParamEdit(label, key, default, is_int, suffix, step)
            edit.valueChanged.connect(self._on_param_changed)
            self._param_edits[key] = edit
            layout.addWidget(edit)
        return group

    def _create_echo_params_group(self):
        group = QGroupBox("回波生成器参数 (Case 6)")
        layout = QVBoxLayout(group)
        for label, key, default, is_int, suffix, step in self._ECHO_PARAMS:
            edit = ParamEdit(label, key, default, is_int, suffix, step)
            edit.valueChanged.connect(self._on_param_changed)
            self._param_edits[key] = edit
            layout.addWidget(edit)
        return group

    # ── 波形参数 (按 Case 子分组, 始终可见) ──
    def _create_waveform_params_group(self):
        group = QGroupBox("波形参数 (Cases 1-5)")
        layout = QVBoxLayout(group)
        for case_num, params in sorted(self._WAVEFORM_PARAMS.items()):
            sep = QLabel(f"── Case {case_num} ──")
            sep.setStyleSheet("color: #6c7086; font-size: 11px;")
            sep.setAlignment(Qt.AlignmentFlag.AlignCenter)
            layout.addWidget(sep)
            for label, key, default, is_int, suffix, step in params:
                edit = ParamEdit(label, key, default, is_int, suffix, step)
                edit.valueChanged.connect(self._on_param_changed)
                self._param_edits[key] = edit
                layout.addWidget(edit)
        return group

    # ── 派生参数 ──
    def _create_derived_info_group(self):
        group = QGroupBox("派生参数 (自动计算)")
        layout = QVBoxLayout(group)
        self._derived_labels = {}
        derived_items = [
            ("采样频率 fs = 3B",         "fs"),
            ("调频斜率 gama = B/Tp",     "gama"),
            ("波长 lambda = c/fc",       "lam"),
            ("脉冲重复周期 prt = 1/prf", "prt"),
            ("距离向采样点 nrn",         "nrn"),
            ("起始采样偏移 Tstart",      "Tstart"),
        ]
        for text, key in derived_items:
            row = QHBoxLayout()
            name_lbl = QLabel(text)
            name_lbl.setMinimumWidth(140)
            name_lbl.setStyleSheet("color: #5a6a7a; font-size: 12px;")
            row.addWidget(name_lbl)
            val_lbl = DerivedLabel()
            row.addWidget(val_lbl, stretch=1)
            self._derived_labels[key] = val_lbl
            layout.addLayout(row)
        self._update_derived_params()
        return group

    def _update_derived_params(self):
        try:
            B   = self._param_edits["system.B"].value()
            Tp  = self._param_edits["system.Tp"].value()
            fc  = self._param_edits["system.fc"].value()
            prf = self._param_edits["system.prf"].value()
            wr  = self._param_edits["system.wr"].value()
            Rs  = self._param_edits["system.Rs"].value()

            c  = 3e8
            fs_val = 3.0 * B
            gama   = B / Tp if Tp != 0 else 0
            lam    = c / fc if fc != 0 else 0
            prt    = 1.0 / prf if prf != 0 else 0
            nrn    = int(math.floor((Tp * fs_val + wr) / 2.0)) * 2
            Tstart = 2.0 * Rs / c - nrn / 2.0 / fs_val if fs_val != 0 else 0

            self._derived_labels["fs"].set_text(f"{fs_val:.4e} Hz")
            self._derived_labels["gama"].set_text(f"{gama:.4e} Hz/s")
            self._derived_labels["lam"].set_text(f"{lam:.6f} m")
            self._derived_labels["prt"].set_text(f"{prt:.4e} s")
            self._derived_labels["nrn"].set_text(f"{nrn}")
            self._derived_labels["Tstart"].set_text(f"{Tstart:.4e} s")
        except Exception:
            pass

    # ── 事件处理 ──
    def _on_param_changed(self, key, value):
        self._update_derived_params()
        self.paramsChanged.emit()

    def _on_restore_defaults(self):
        self.restoreDefaultsRequested.emit()

    def _on_run(self):
        modes = self.get_selected_modes()
        if not modes:
            return
        config_dict = self.get_all_params()
        self.runRequested.emit(modes)

    # ── 公共接口 ──
    def get_selected_modes(self):
        """返回 [(func_type, arg), ...] 列表"""
        modes = []
        for label, cb in self._type_checks.items():
            if cb.isChecked():
                if label in _JAMMING_CASES:
                    jam_idx = self._jamming_combo.currentIndex()
                    if jam_idx >= 0:
                        jam_type = self._jamming_combo.itemData(jam_idx)
                        modes.append(("processing_decouple", jam_type))
                else:
                    modes.append(("processing_rd", label))
        return modes

    def set_running(self, running):
        self._run_btn.setEnabled(not running)
        self._stop_btn.setEnabled(running)
        self._lock_btn.setEnabled(not running)
        if running:
            for edit in self._param_edits.values():
                edit._spin.setEnabled(False)
            for cb in self._type_checks.values():
                cb.setEnabled(False)
            self._jamming_combo.setEnabled(False)
        else:
            self._apply_lock_state()

    def get_all_params(self):
        params = {}
        for key, edit in self._param_edits.items():
            params[key] = edit.value()
        return params

    def set_params(self, params):
        for key, val in params.items():
            if key in self._param_edits:
                self._param_edits[key].set_value(val)
        self._update_derived_params()
