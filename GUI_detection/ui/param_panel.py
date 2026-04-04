import math

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QLabel, QPushButton, QScrollArea, QFrame,
    QCheckBox, QSizePolicy, QSpinBox,
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


class ParamPanel(QWidget):

    paramsChanged = Signal()
    runRequested = Signal(list)         # 多选模式, 传 label 列表
    stopRequested = Signal()
    clearRequested = Signal()
    restoreDefaultsRequested = Signal()

    # ── 组1: 系统参数 (EchoGenerator.h) ──
    _SYSTEM_PARAMS = [
        ("载波频率 fc (Hz)",       "detection_suppression.fc",           35e9,   False, "", 1e9),
        ("信号带宽 B (Hz)",        "detection_suppression.B",            80e6,   False, "", 1e6),
        ("采样频率 fs (Hz)",       "detection_suppression.fs",           120e6,  False, "", 1e6),
        ("目标初始距离 R0 (m)",    "detection_suppression.R0",           1000,   False, "", 100),
        ("脉冲重复频率 prf (Hz)",  "detection_suppression.prf",          5e3,    False, "", 1e3),
        ("脉冲宽度 Tp (s)",        "detection_suppression.Tp",           12e-6,  False, "", 1e-6),
        ("信号采样点数 nrn",       "detection_suppression.nrn",          2048,   True,  "", 1),
        ("CPI 数量 cpiNum",        "detection_suppression.cpiNum",       100,    True,  "", 1),
        ("信噪比 SNR (dB)",        "detection_suppression.SNR",          25,     False, "", 1),
        ("干信比 JSR (dB)",        "detection_suppression.JSR",          30,     False, "", 1),
        ("底噪 SNR (dB)",          "detection_suppression.noise_snr",    25,     False, "", 1),
        ("干扰距离下限比例",       "detection_suppression.r_min_ratio",  0.7,    False, "", 0.1),
        ("干扰距离上限比例",       "detection_suppression.r_max_ratio",  1.7,    False, "", 0.1),
    ]

    # ── 检测与抑制专属参数 (带分隔线分组, 与 01/02 风格一致) ──
    _DETECTION_PARAMS = [
        ("── 识别参数 (GrDetection) ──", [
            ("识别 STFT 频点数",      "detection_suppression.gr_stft_num",    256, True,  "", 1),
            ("识别汉明窗长度",        "detection_suppression.gr_hamming_len", 63,  True,  "", 1),
        ]),
        ("── 分离参数 (JamTarDivi) ──", [
            ("分离 STFT 频点数",      "detection_suppression.divi_stft_num",      256,  True,  "", 1),
            ("分离汉明窗长度",        "detection_suppression.divi_hamming_len",   31,   True,  "", 1),
            ("Tsallis 熵参数 q",      "detection_suppression.tsallis_q",          1.2,  False, "", 0.1),
            ("高阶谱判定阈值",        "detection_suppression.gaojiepu_threshold", 35,   False, "", 1),
        ]),
        ("── 时频分析参数 (TfrStft) ──", [
            ("STFT 缩放因子",         "detection_suppression.scale_factor", 32768, True, "", 1),
        ]),
        ("── 干扰生成器参数 (JammingSimulator) ──", [
            ("ISCJ 子脉冲宽度 (s)",       "detection_suppression.iscj_sub_T",       1e-6,  False, "", 1e-6),
            ("ISDJ 重复周期 (s)",         "detection_suppression.isdj_sub_Ts",      2e-6,  False, "", 1e-6),
            ("ISRJ 重复周期 (s)",         "detection_suppression.isrj_sub_Ts",      4e-6,  False, "", 1e-6),
            ("NBJ 频带中心偏移 (Hz)",     "detection_suppression.nbj_center_freq",  15e6,  False, "", 1e6),
            ("NBJ 高斯噪声标准差",        "detection_suppression.nbj_noise_std",    5.0,   False, "", 0.1),
            ("NBJ 滤波器指数阶",          "detection_suppression.nbj_filter_order", 8,     True,  "", 1),
        ]),
    ]

    # 5 种干扰类型: label → 中文名
    _JAMMING_TYPES = {
        1: "Label 1: 间歇采样直接转发 (ISDJ)",
        2: "Label 2: 间歇采样重复转发 (ISRJ)",
        3: "Label 3: 间歇采样循环转发 (ISCJ)",
        4: "Label 4: 窄带噪声 (NBJ)",
        5: "Label 5: 距离欺骗干扰 (RDJ)",
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self._param_edits = {}
        self._locked = False
        self._type_checks = {}         # label → QCheckBox
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
        layout.addWidget(self._create_detection_params_group())
        layout.addWidget(self._create_derived_info_group())
        layout.addStretch()

        scroll.setWidget(container)
        main_layout.addWidget(scroll)

    # ── 干扰类型选择 (复选框, 与 01/02 一致) ──
    def _create_type_select_group(self):
        group = QGroupBox("干扰类型选择")
        layout = QVBoxLayout(group)

        self._type_checks = {}
        for label, name in self._JAMMING_TYPES.items():
            cb = QCheckBox(name)
            cb.setChecked(True)
            cb.setProperty("jamming_label", label)
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

    def _create_system_params_group(self):
        group = QGroupBox("系统参数 (EchoGenerator)")
        layout = QVBoxLayout(group)
        for label, key, default, is_int, suffix, step in self._SYSTEM_PARAMS:
            edit = ParamEdit(label, key, default, is_int, suffix, step)
            edit.valueChanged.connect(self._on_param_changed)
            self._param_edits[key] = edit
            layout.addWidget(edit)
        return group

    def _create_detection_params_group(self):
        group = QGroupBox("检测与抑制参数")
        layout = QVBoxLayout(group)
        for sep_text, params in self._DETECTION_PARAMS:
            sep = QLabel(sep_text)
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
            ("调频斜率 Kr = B/Tp",   "Kr"),
            ("波长 lambda = c/fc",    "lambda"),
            ("脉冲重复周期 prt = 1/prf", "prt"),
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
            B  = self._param_edits["detection_suppression.B"].value()
            Tp = self._param_edits["detection_suppression.Tp"].value()
            fc = self._param_edits["detection_suppression.fc"].value()
            prf = self._param_edits["detection_suppression.prf"].value()

            Kr   = B / Tp if Tp != 0 else 0
            lam  = 3e8 / fc if fc != 0 else 0
            prt  = 1.0 / prf if prf != 0 else 0

            self._derived_labels["Kr"].set_text(f"{Kr:.4e} Hz/s")
            self._derived_labels["lambda"].set_text(f"{lam:.6f} m")
            self._derived_labels["prt"].set_text(f"{prt:.4e} s")
        except Exception:
            pass

    # ── 事件处理 ──
    def _on_param_changed(self, key, value):
        self._update_derived_params()
        self.paramsChanged.emit()

    def _on_restore_defaults(self):
        self.restoreDefaultsRequested.emit()

    def _on_run(self):
        labels = [m for m, cb in self._type_checks.items() if cb.isChecked()]
        if not labels:
            return
        self.runRequested.emit(labels)

    # ── 公共接口 ──
    def set_running(self, running):
        self._run_btn.setEnabled(not running)
        self._stop_btn.setEnabled(running)
        self._lock_btn.setEnabled(not running)
        if running:
            for edit in self._param_edits.values():
                edit._spin.setEnabled(False)
            for cb in self._type_checks.values():
                cb.setEnabled(False)
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

    def get_selected_modes(self):
        return [m for m, cb in self._type_checks.items() if cb.isChecked()]
