import math

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QLabel, QDoubleSpinBox, QSpinBox, QCheckBox,
    QPushButton, QScrollArea, QFrame, QSizePolicy,
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
    runRequested = Signal(list)     # 多选模式, 传 mode 列表
    stopRequested = Signal()
    clearRequested = Signal()
    restoreDefaultsRequested = Signal()

    _SYSTEM_PARAMS = [
        ("载波频率 fc (Hz)", "system.fc", 16e9, False, "", 1e9),
        ("脉冲宽度 Tp (s)", "system.Tp", 12e-6, False, "", 1e-6),
        ("信号带宽 B (Hz)", "system.B", 40e6, False, "", 1e6),
        ("脉冲重复频率 prf (Hz)", "system.prf", 10e3, False, "", 1e3),
        ("相对径向速度 Vr (m/s)", "system.Vr", 50.0, False, "", 10),
        ("场景中心斜距 Rs (m)", "system.Rs", 10000.0, False, "", 1000),
        ("距离向宽度 wr (m)", "system.wr", 608.0, False, "", 50),
        ("干扰增益 A_RJ (dB)", "system.A_RJ", 10.0, False, "", 1),
        ("雷达初始高度 z_R0 (m)", "system.z_R0", 2000.0, False, "", 100),
        ("方位向脉冲数 nan1", "system.nan1", 64, True, "", 1),
        ("距离走动放大因子", "system.range_walk_factor", 4000.0, False, "", 1000),
    ]

    _JAMMING_PARAMS = [
        ("── Case1: 距离假目标 (RDJ) ──", [
            ("干扰脉冲延迟数 jj", "jamming.case1_rdj.jj", 1, True, "", 1),
            ("干扰目标距离 Rj (m)", "jamming.case1_rdj.Rj", 100.0, False, "", 10),
            ("干扰幅度增益 amp_j", "jamming.case1_rdj.amp_j", 10.0, False, "", 1),
            ("目标幅度", "jamming.case1_rdj.amp_target", 1.0, False, "", 0.1),
            ("AWGN 信噪比 (dB)", "jamming.case1_rdj.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case2: 速度假目标 (VDJ) ──", [
            ("假目标速度 Vj (m/s)", "jamming.case2_vdj.Vj", 1e5, False, "", 1e3),
            ("干扰延迟脉冲数 jj", "jamming.case2_vdj.jj", 1, True, "", 1),
            ("假目标距离 Rj (m)", "jamming.case2_vdj.Rj", 10.0, False, "", 1),
            ("干扰幅度增益 amp_j", "jamming.case2_vdj.amp_j", 10.0, False, "", 1),
            ("目标幅度", "jamming.case2_vdj.amp_target", 1.0, False, "", 0.1),
            ("AWGN 信噪比 (dB)", "jamming.case2_vdj.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case3: 间歇采样转发 (ISRJ) ──", [
            ("采样周期 Ts_ISRJ (s)", "jamming.case3_isrj.Ts_ISRJ", 4e-6, False, "", 1e-6),
            ("采样脉宽 T_ISRJ (s)", "jamming.case3_isrj.T_ISRJ", 0.0, False, "", 1e-6),
            ("干扰目标距离 Rj (m)", "jamming.case3_isrj.Rj", 10.0, False, "", 1),
            ("干扰幅度增益 amp_j", "jamming.case3_isrj.amp_j", 10.0, False, "", 1),
            ("目标幅度", "jamming.case3_isrj.amp_target", 1.0, False, "", 0.1),
            ("AWGN 信噪比 (dB)", "jamming.case3_isrj.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case4: 窄带噪声 (NNJ) ──", [
            ("噪声功率 (dBW)", "jamming.case4_nnj.power_dBW", 20.0, False, "", 1),
            ("低通滤波器阶数", "jamming.case4_nnj.butter_order", 8, True, "", 1),
            ("归一化截止频率", "jamming.case4_nnj.butter_cutoff", 0.3, False, "", 0.05),
            ("目标幅度", "jamming.case4_nnj.amp_target", 1.0, False, "", 0.1),
            ("AWGN 信噪比 (dB)", "jamming.case4_nnj.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case5: 距离波门拖引 (RGPO) ──", [
            ("假目标拖引速度 Vj (m/s)", "jamming.case5_rgpo.Vj", 340.0, False, "", 10),
            ("目标幅度 amp_target", "jamming.case5_rgpo.amp_target", 1.0, False, "", 0.1),
            ("干扰幅度 amp_jammer", "jamming.case5_rgpo.amp_jammer", 1.4, False, "", 0.1),
            ("拖引阶段次数", "jamming.case5_rgpo.drag_stages", 4, True, "", 1),
            ("AWGN 信噪比 (dB)", "jamming.case5_rgpo.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case6: 速度波门拖引 (VGPO) ──", [
            ("假目标速度 Vj (m/s)", "jamming.case6_vgpo.Vj", 1e4, False, "", 1e3),
            ("目标幅度 amp_target", "jamming.case6_vgpo.amp_target", 1.0, False, "", 0.1),
            ("干扰幅度 amp_jammer", "jamming.case6_vgpo.amp_jammer", 1.4, False, "", 0.1),
            ("拖引阶段次数", "jamming.case6_vgpo.drag_stages", 4, True, "", 1),
            ("速度拖引放大因子", "jamming.case6_vgpo.velocity_drag_factor", 40000.0, False, "", 1000),
            ("AWGN 信噪比 (dB)", "jamming.case6_vgpo.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case7: 密集假目标 (DRFTJ) ──", [
            ("干信比 JSR (dB)", "jamming.case7_drftj.JSR", 0.0, False, "", 1),
            ("目标幅度 amp_target", "jamming.case7_drftj.amp_target", 1.0, False, "", 0.1),
            ("转发次数 num_jam", "jamming.case7_drftj.num_jam", 50, True, "", 1),
            ("距离增量 detaR (m)", "jamming.case7_drftj.detaR", 50.0, False, "", 10),
            ("前向转发次数", "jamming.case7_drftj.forward_replicas", 3, True, "", 1),
            ("AWGN 信噪比 (dB)", "jamming.case7_drftj.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case8: 脉内前沿切片 (IPLESRJ) ──", [
            ("干扰功率增益 A_RJ (dB)", "jamming.case8_iplesrj.A_RJ", 30.0, False, "", 1),
            ("目标幅度 amp_target", "jamming.case8_iplesrj.amp_target", 1.0, False, "", 0.1),
            ("雷达-目标距离 R0_new (m)", "jamming.case8_iplesrj.R0_new", 608.0, False, "", 10),
            ("径向速度 V_ISRJ (m/s)", "jamming.case8_iplesrj.V_ISRJ", 0.0, False, "", 1),
            ("干扰间隔 Tp/ratio", "jamming.case8_iplesrj.T_ISRJ_ratio", 16, True, "", 1),
            ("提前发射时间 R_ahead (s)", "jamming.case8_iplesrj.R_ahead", 0.0, False, "", 1e-6),
            ("AWGN 信噪比 (dB)", "jamming.case8_iplesrj.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case9: 频谱弥散 (SMSP) ──", [
            ("频谱切片次数", "jamming.case9_smsp.num_slices", 4, True, "", 1),
            ("干信比 JSR (dB)", "jamming.case9_smsp.JSR", 15.0, False, "", 1),
            ("目标幅度 amp_target", "jamming.case9_smsp.amp_target", 1.0, False, "", 0.1),
            ("干扰幅度额外系数", "jamming.case9_smsp.amp_extra", 1.4, False, "", 0.1),
            ("目标初始距离 R0 (m)", "jamming.case9_smsp.R0", 10000.0, False, "", 1000),
            ("AWGN 信噪比 (dB)", "jamming.case9_smsp.awgn_snr", 10.0, False, "", 1),
        ]),
        ("── Case10: 梳状谱 (COMB) ──", [
            ("频谱线数量", "jamming.case10_comb.num_tones", 7, True, "", 1),
            ("干信比 JSR (dB)", "jamming.case10_comb.JSR", 0.0, False, "", 1),
            ("目标幅度 amp_target", "jamming.case10_comb.amp_target", 1.0, False, "", 0.1),
            ("频率间隔 deltaf (Hz)", "jamming.case10_comb.deltaf", 1e6, False, "", 1e5),
            ("目标初始距离 R0 (m)", "jamming.case10_comb.R0", 10000.0, False, "", 1000),
            ("AWGN 信噪比 (dB)", "jamming.case10_comb.awgn_snr", 10.0, False, "", 1),
        ]),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._param_edits = {}
        self._locked = False
        self._case_checks = {}         # mode -> QCheckBox
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

        layout.addWidget(self._create_case_select_group())
        layout.addWidget(self._create_action_buttons())
        layout.addWidget(self._create_lock_button())
        layout.addWidget(self._create_system_params_group())
        layout.addWidget(self._create_jamming_params_group())
        layout.addWidget(self._create_derived_info_group())
        layout.addStretch()

        scroll.setWidget(container)
        main_layout.addWidget(scroll)

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
        for cb in self._case_checks.values():
            cb.setEnabled(not self._locked)

    def _create_case_select_group(self):
        group = QGroupBox("干扰模式选择")
        layout = QVBoxLayout(group)

        self._case_checks = {}
        names = {
            1: "Case1: 距离假目标 (RDJ)",
            2: "Case2: 速度假目标 (VDJ)",
            3: "Case3: 间歇采样转发 (ISRJ)",
            4: "Case4: 窄带噪声 (NNJ)",
            5: "Case5: 距离波门拖引 (RGPO)",
            6: "Case6: 速度波门拖引 (VGPO)",
            7: "Case7: 密集假目标 (DRFTJ)",
            8: "Case8: 脉内前沿切片 (IPLESRJ)",
            9: "Case9: 频谱弥散 (SMSP)",
            10: "Case10: 梳状谱 (COMB)",
        }

        for mode, name in names.items():
            cb = QCheckBox(name)
            cb.setChecked(True)
            cb.setProperty("case_mode", mode)
            self._case_checks[mode] = cb
            layout.addWidget(cb)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(10)
        select_all = QPushButton("✓ 全选")
        select_all.setObjectName("selectAllBtn")
        select_all.setMinimumWidth(64)
        select_all.clicked.connect(lambda: [c.setChecked(True) for c in self._case_checks.values()])
        deselect_all = QPushButton("✕ 全不选")
        deselect_all.setObjectName("deselectAllBtn")
        deselect_all.setMinimumWidth(64)
        deselect_all.clicked.connect(lambda: [c.setChecked(False) for c in self._case_checks.values()])
        btn_row.addWidget(select_all)
        btn_row.addWidget(deselect_all)
        btn_row.addStretch()
        layout.addLayout(btn_row)

        return group

    def _create_system_params_group(self):
        group = QGroupBox("系统基本参数")
        layout = QVBoxLayout(group)
        for label, key, default, is_int, suffix, step in self._SYSTEM_PARAMS:
            edit = ParamEdit(label, key, default, is_int, suffix, step)
            edit.valueChanged.connect(self._on_param_changed)
            self._param_edits[key] = edit
            layout.addWidget(edit)
        return group

    def _create_jamming_params_group(self):
        group = QGroupBox("干扰专属参数")
        layout = QVBoxLayout(group)
        for sep_text, params in self._JAMMING_PARAMS:
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

    def _create_derived_info_group(self):
        group = QGroupBox("派生参数 (自动计算)")
        layout = QVBoxLayout(group)
        self._derived_labels = {}
        derived_items = [
            ("fs = 3B", "fs"),
            ("gama = B/Tp", "gama"),
            ("lambda = c/fc", "lambda"),
            ("prt = 1/prf", "prt"),
            ("nrn", "nrn"),
            ("amp_j (线性)", "amp_j"),
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

    def _on_param_changed(self, key, value):
        self._update_derived_params()
        self.paramsChanged.emit()

    def _update_derived_params(self):
        try:
            B = self._param_edits["system.B"].value()
            Tp = self._param_edits["system.Tp"].value()
            fc = self._param_edits["system.fc"].value()
            prf = self._param_edits["system.prf"].value()
            wr = self._param_edits["system.wr"].value()
            A_RJ = self._param_edits["system.A_RJ"].value()
            fs = 3.0 * B
            gama = B / Tp if Tp != 0 else 0
            lam = 3e8 / fc if fc != 0 else 0
            prt = 1.0 / prf if prf != 0 else 0
            nrn = int(math.floor((Tp * fs + wr) / 2.0)) * 2
            amp_j = 10 ** (A_RJ / 20.0)

            self._derived_labels["fs"].set_text(f"{fs:.4e} Hz")
            self._derived_labels["gama"].set_text(f"{gama:.4e} Hz/s")
            self._derived_labels["lambda"].set_text(f"{lam:.6f} m")
            self._derived_labels["prt"].set_text(f"{prt:.4e} s")
            self._derived_labels["nrn"].set_text(f"{nrn}")
            self._derived_labels["amp_j"].set_text(f"{amp_j:.4f}")
        except Exception:
            pass

    def _on_restore_defaults(self):
        self.restoreDefaultsRequested.emit()

    def _on_run(self):
        modes = [m for m, cb in self._case_checks.items() if cb.isChecked()]
        if not modes:
            return
        self.runRequested.emit(modes)

    def set_running(self, running):
        self._run_btn.setEnabled(not running)
        self._stop_btn.setEnabled(running)
        self._lock_btn.setEnabled(not running)
        if running:
            for edit in self._param_edits.values():
                edit._spin.setEnabled(False)
            for cb in self._case_checks.values():
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
        return [m for m, cb in self._case_checks.items() if cb.isChecked()]
