"""
可视化面板 — 干扰识别与抑制 GUI (7种图表)

1. 时域波形 — 带噪含干扰回波/含噪目标/分离干扰/分离目标 (实部/虚部/包络)
2. 频域频谱 — FFT 幅度 (dB)
3. 含干扰回波 STFT — 第0脉冲带噪含干扰回波 STFT (C++ 端计算)
4. 干扰定位 — 含干扰回波 STFT 叠加 Otsu 二值掩码
5. 分离对比 — 含干扰回波/干扰/目标三线叠加
6. 分离时频 — 分离后干扰/目标各自 STFT (scipy)
7. 检测统计 — 柱状图: 各类型投票数 + ISR
"""

import os
import sys
import numpy as np
import warnings
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTabWidget,
    QLabel, QPushButton, QFileDialog, QMessageBox,
    QButtonGroup, QComboBox, QSpinBox,
)
from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor

import pyqtgraph as pg
from scipy.fft import fft, fftshift
from scipy.signal import stft as scipy_stft

from ui.theme import PLOT_THEMES, WIDGET_THEMES

# ── Module-level theme state ──
_current_plot_theme = "light"
_USE_OPENGL = sys.platform == "win32"
_AA = True


def apply_plot_theme(theme="light"):
    global _current_plot_theme
    _current_plot_theme = theme
    pt = PLOT_THEMES.get(theme, PLOT_THEMES["light"])
    pg.setConfigOptions(
        background=pt["pg_bg"],
        foreground=pt["pg_fg"],
        antialias=_AA,
        useOpenGL=_USE_OPENGL,
        enableExperimental=True,
    )


apply_plot_theme("light")


def _pt():
    return PLOT_THEMES.get(_current_plot_theme, PLOT_THEMES["light"])


_TICK_FONT = pg.QtGui.QFont("Menlo", 10)
_LABEL_STYLE = {'color': '#5a6a7a', 'font-size': '11pt', 'font-family': 'PingFang SC'}


def _set_label(plot_item_or_axis, axis, text, units=None):
    style = dict(_LABEL_STYLE)
    style['color'] = _pt()["text"]
    kwargs = dict(style)
    if units:
        plot_item_or_axis.setLabel(axis, text, units=units, **kwargs)
    else:
        plot_item_or_axis.setLabel(axis, text, **kwargs)


def _apply_axis_style(plot_item):
    pt = _pt()
    for name in ('left', 'bottom'):
        ax = plot_item.getAxis(name)
        ax.setPen(pg.mkPen(pt["axis"], width=1))
        ax.setTextPen(pg.mkPen(pt["text"]))
        ax.setTickFont(_TICK_FONT)


def _make_sig_buttons(parent, group_name, items, default_idx):
    """创建信号类型切换按钮组, 返回 (layout, QButtonGroup)"""
    pt = _pt()
    btn_row = QHBoxLayout()
    btn_row.setSpacing(4)
    btn_group = QButtonGroup(parent)
    btn_group.setExclusive(True)
    for idx, (name, color_key) in enumerate(items):
        color = pt[color_key]
        btn = QPushButton(name)
        btn.setObjectName(group_name)
        btn.setCheckable(True)
        btn.setChecked(idx == default_idx)
        btn.setStyleSheet(
            f"QPushButton[{group_name}] {{ color: {color}; border: 1.5px solid {color}; "
            f"background: transparent; border-radius: 4px; padding: 2px 12px; "
            f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
            f"QPushButton[{group_name}]:checked {{ background: {color}; color: #fff; }}"
        )
        btn_group.addButton(btn, idx)
        btn_row.addWidget(btn)
    btn_row.addStretch()
    return btn_row, btn_group


# ── 1. 时域波形 ──

class TimeDomainPlot(QWidget):

    _SIG_ITEMS = [
        ("带噪含干扰回波", "blue"), ("含噪目标", "orange"),
        ("分离干扰", "red"), ("分离目标", "green"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号类型 + 波形类型
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(8)

        sig_row, self._sig_group = _make_sig_buttons(self, "sigToggle", self._SIG_ITEMS, 0)
        for btn in sig_row.findChildren(QPushButton):
            ctrl_row.addWidget(btn)
        spacer = ctrl_row.addSpacing(20)

        wave_row, self._wave_group = _make_sig_buttons(
            self, "waveToggle",
            [("实部", "blue"), ("虚部", "red"), ("包络", "green")],
            2,
        )
        for btn in wave_row.findChildren(QPushButton):
            ctrl_row.addWidget(btn)
        ctrl_row.addStretch()

        self._pw = pg.PlotWidget(axisItems={'left': pg.AxisItem('left')})
        self._pw.setTitle("时域波形", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "快时间", units="us")
        _set_label(self._pw, "left", "幅度")

        self._curve = self._pw.plot(pen=pg.mkPen(pt["blue"], width=1.5),
                                    clipToView=True, downsample=2)

        # 十字准线
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._pw.addItem(self._vline, ignoreBounds=True)
        self._pw.addItem(self._hline, ignoreBounds=True)
        self._crosshair = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                       fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair.setPos(0, 0)
        self._crosshair.setVisible(False)
        self._pw.addItem(self._crosshair, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        layout.addLayout(ctrl_row)
        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 0
        self._wave_type = 2
        self._t_axis = None
        self._cols = [None] * 4

        self._sig_group.idToggled.connect(self._on_sig_toggle)
        self._wave_group.idToggled.connect(self._on_wave_toggle)

    def _on_mouse_moved(self, evt):
        pos = self._pw.getViewBox().mapSceneToView(evt)
        if self._pw.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            x, y = pos.x(), pos.y()
            self._crosshair.setText(f"t={x:.3f} us  amp={y:.4f}")
            self._crosshair.setPos(x, y)
            self._crosshair.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair.setVisible(False)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._apply_data()

    def _on_wave_toggle(self, btn_id, checked):
        if checked:
            self._wave_type = btn_id
            self._apply_data()

    def _apply_data(self):
        if self._t_axis is None:
            return
        col = self._cols[self._sig_type]
        if col is None:
            return
        self._update_curve(col)
        self._pw.getViewBox().autoRange()

    def _update_curve(self, col):
        if self._wave_type == 0:
            self._curve.setData(self._t_axis * 1e6, col.real)
        elif self._wave_type == 1:
            self._curve.setData(self._t_axis * 1e6, col.imag)
        else:
            self._curve.setData(self._t_axis * 1e6, np.abs(col))

        sig_names = ["带噪含干扰回波", "含噪目标", "分离干扰", "分离目标"]
        wave_names = ["实部", "虚部", "包络"]
        self._pw.setTitle(
            f"时域波形 — {sig_names[self._sig_type]} ({wave_names[self._wave_type]})",
            color=_pt()["pg_fg"], size="11pt",
        )

    def update_plot(self, t_axis, cols):
        """t_axis: (nrn,), cols: list of 4 signals (nrn,)"""
        if t_axis is None or len(t_axis) == 0:
            return
        self._t_axis = t_axis
        self._cols = cols
        self._update_curve(cols[self._sig_type])
        self._pw.getViewBox().autoRange()

    def clear_data(self):
        self._curve.setData([], [])
        self._t_axis = None
        self._cols = [None] * 4


# ── 2. 频域频谱 ──

class FreqDomainPlot(QWidget):

    _SIG_ITEMS = [
        ("带噪含干扰回波", "blue"), ("含噪目标", "orange"),
        ("分离干扰", "red"), ("分离目标", "green"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        ctrl_row, self._sig_group = _make_sig_buttons(self, "sigToggle", self._SIG_ITEMS, 0)

        self._pw = pg.PlotWidget(axisItems={'left': pg.AxisItem('left')})
        self._pw.setTitle("频谱 (幅度)", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "频率", units="MHz")
        _set_label(self._pw, "left", "幅度", units="dB")

        self._curve = self._pw.plot(pen=pg.mkPen(pt["purple"], width=1.5),
                                    clipToView=True, downsample=2)

        # 十字准线
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._pw.addItem(self._vline, ignoreBounds=True)
        self._pw.addItem(self._hline, ignoreBounds=True)
        self._crosshair = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                       fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair.setPos(0, 0)
        self._crosshair.setVisible(False)
        self._pw.addItem(self._crosshair, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        layout.addLayout(ctrl_row)
        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 0
        self._fr = None
        self._cols = [None] * 4

        self._sig_group.idToggled.connect(self._on_sig_toggle)

    def _on_mouse_moved(self, evt):
        pos = self._pw.getViewBox().mapSceneToView(evt)
        if self._pw.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            x, y = pos.x(), pos.y()
            self._crosshair.setText(f"f={x:.2f} MHz  |H|={y:.1f} dB")
            self._crosshair.setPos(x, y)
            self._crosshair.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair.setVisible(False)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._apply_data()

    def _apply_data(self):
        if self._fr is None:
            return
        col = self._cols[self._sig_type]
        if col is None:
            return
        spec = fftshift(fft(col))
        mag = 20.0 * np.log10(np.abs(spec) + 1e-12)
        self._curve.setData(self._fr / 1e6, mag)
        sig_names = ["带噪含干扰回波", "含噪目标", "分离干扰", "分离目标"]
        self._pw.setTitle(f"频谱 — {sig_names[self._sig_type]}",
                          color=_pt()["pg_fg"], size="11pt")
        self._pw.getViewBox().autoRange()

    def update_plot(self, fr, cols):
        """fr: (nrn,) freq axis, cols: list of 4 signals"""
        if fr is None or len(fr) == 0:
            return
        self._fr = fr
        self._cols = cols
        self._apply_data()

    def clear_data(self):
        self._curve.setData([], [])
        self._fr = None
        self._cols = [None] * 4


# ── 3. STFT 时频图 (C++ 端计算) ──

class STFTPlotWidget(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="带噪含干扰回波 STFT 时频图 (C++)",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "时间采样点")
        _set_label(self._plot, "left", "频率点")

        self._img = pg.ImageItem()
        jet_colors = [
            (0.0,  (0, 0, 143)),   (0.12, (0, 0, 255)),
            (0.25, (0, 127, 255)), (0.37, (0, 255, 255)),
            (0.5,  (0, 255, 0)),   (0.62, (255, 255, 0)),
            (0.75, (255, 127, 0)), (0.87, (255, 0, 0)),
            (1.0,  (128, 0, 0)),
        ]
        self._jet_cmap = pg.ColorMap(
            pos=[c[0] for c in jet_colors],
            color=[c[1] for c in jet_colors],
        )
        self._img.setColorMap(self._jet_cmap)
        self._plot.addItem(self._img)

        self._lut = pg.HistogramLUTItem(image=self._img)
        self._lut.gradient.setColorMap(self._jet_cmap)
        self._layout_widget.addItem(self._lut)

        layout.addWidget(self._layout_widget, stretch=1)

    def update_plot(self, stft_matrix):
        """stft_matrix: (STFT_NUM × nrn) complex from C++"""
        if stft_matrix is None:
            return
        mag_db = 20.0 * np.log10(np.abs(stft_matrix) + 1e-12)
        peak = float(np.max(mag_db))
        vmin = peak - 60.0
        vmax = peak

        self._img.setImage(mag_db[::-1], autoLevels=False)
        self._img.setLevels([vmin, vmax])
        self._lut.setLevels(vmin, vmax)

        n_freq, n_time = stft_matrix.shape
        self._img.setRect(pg.QtCore.QRectF(0, 0, n_time, n_freq))
        self._plot.setTitle(f"带噪含干扰回波 STFT 时频图 ({n_freq} x {n_time})",
                            color=_pt()["pg_fg"], size="11pt")
        _set_label(self._plot, "bottom", "时间采样点", units=f"  [0~{n_time - 1}]")
        _set_label(self._plot, "left", "频率点", units=f"  [0~{n_freq - 1}]")
        self._plot.getViewBox().setLimits(
            xMin=-0.5, xMax=n_time - 0.5,
            yMin=-0.5, yMax=n_freq - 0.5,
        )
        self._plot.getViewBox().setRange(
            xRange=(-0.5, n_time - 0.5),
            yRange=(-0.5, n_freq - 0.5),
            padding=0.02,
        )

    def clear_data(self):
        self._img.clear()


# ── 4. 干扰定位 (STFT + Otsu 掩码叠加) ──

class JamMaskPlotWidget(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="含干扰回波 干扰定位 (Otsu)",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "时间采样点")
        _set_label(self._plot, "left", "频率点")

        # STFT 背景 (灰度)
        self._img_bg = pg.ImageItem()
        self._plot.addItem(self._img_bg)

        # 掩码叠加 (红色半透明)
        self._img_mask = pg.ImageItem()
        self._plot.addItem(self._img_mask)

        self._lut = pg.HistogramLUTItem(image=self._img_bg)
        self._layout_widget.addItem(self._lut)

        layout.addWidget(self._layout_widget, stretch=1)

    def update_plot(self, stft_matrix, jam_mask):
        """stft_matrix: (STFT_NUM × nrn) complex, jam_mask: (STFT_NUM × nrn) binary"""
        if stft_matrix is None or jam_mask is None:
            return

        mag_db = 20.0 * np.log10(np.abs(stft_matrix) + 1e-12)
        peak = float(np.max(mag_db))
        vmin = peak - 60.0
        vmax = peak

        # 灰度 STFT 背景
        self._img_bg.setImage(mag_db[::-1], autoLevels=False)
        self._img_bg.setLevels([vmin, vmax])
        self._lut.setLevels(vmin, vmax)

        # 红色掩码: jam_mask=1 → RGBA 红色, jam_mask=0 → 透明
        n_freq, n_time = jam_mask.shape
        mask_rgba = np.zeros((n_freq, n_time, 4), dtype=np.uint8)
        mask_rgba[:, :, 0] = (jam_mask[::-1] > 0.5).astype(np.uint8) * 255  # R
        mask_rgba[:, :, 3] = (jam_mask[::-1] > 0.5).astype(np.uint8) * 100  # A (半透明)
        self._img_mask.setImage(mask_rgba)

        rect = pg.QtCore.QRectF(0, 0, n_time, n_freq)
        self._img_bg.setRect(rect)
        self._img_mask.setRect(rect)

        jam_count = int(np.sum(jam_mask > 0.5))
        total = jam_mask.size
        self._plot.setTitle(
            f"含干扰回波 干扰定位 — 红色=Otsu干扰区域 (干扰时频单元/总时频单元 = {jam_count}/{total} = {100*jam_count/total:.1f}%)",
            color=_pt()["pg_fg"], size="11pt",
        )
        _set_label(self._plot, "bottom", "时间采样点", units=f"  [0~{n_time - 1}]")
        _set_label(self._plot, "left", "频率点", units=f"  [0~{n_freq - 1}]")
        self._plot.getViewBox().setLimits(
            xMin=-0.5, xMax=n_time - 0.5,
            yMin=-0.5, yMax=n_freq - 0.5,
        )
        self._plot.getViewBox().setRange(
            xRange=(-0.5, n_time - 0.5),
            yRange=(-0.5, n_freq - 0.5),
            padding=0.02,
        )

    def clear_data(self):
        self._img_bg.clear()
        self._img_mask.clear()


# ── 5. 分离对比 (三线叠加) ──

class SeparationPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号类型切换按钮
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(4)
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("含干扰回波", "blue"), ("分离干扰", "red"),
            ("分离目标", "green"), ("全部叠加", "purple"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("sigToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 3)
            btn.setStyleSheet(
                f"QPushButton[sigToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[sigToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._sig_group.addButton(btn, idx)
            ctrl_row.addWidget(btn)
        ctrl_row.addStretch()
        layout.addLayout(ctrl_row)

        self._pw = pg.PlotWidget(axisItems={'left': pg.AxisItem('left')})
        self._pw.setTitle("分离对比", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "快时间", units="us")
        _set_label(self._pw, "left", "包络幅度")

        self._c_echo = self._pw.plot(pen=pg.mkPen(pt["blue"], width=1.5),
                                     name="含干扰回波", clipToView=True, downsample=2)
        self._c_jam = self._pw.plot(pen=pg.mkPen(pt["red"], width=1.5),
                                    name="分离干扰", clipToView=True, downsample=2)
        self._c_tgt = self._pw.plot(pen=pg.mkPen(pt["green"], width=1.5),
                                    name="分离目标", clipToView=True, downsample=2)

        lg = self._pw.addLegend(offset=(10, 10), labelTextColor=pt["text"],
                                brush=pg.mkBrush(255, 255, 255, 220),
                                pen=pg.mkPen(pt["axis"]))
        lg.addItem(self._c_echo, "含干扰回波")
        lg.addItem(self._c_jam, "分离干扰")
        lg.addItem(self._c_tgt, "分离目标")

        # 十字准线
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._pw.addItem(self._vline, ignoreBounds=True)
        self._pw.addItem(self._hline, ignoreBounds=True)
        self._crosshair = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                       fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair.setPos(0, 0)
        self._crosshair.setVisible(False)
        self._pw.addItem(self._crosshair, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 3  # 0=echo, 1=jam, 2=tgt, 3=all
        self._t_axis = None

        self._sig_group.idToggled.connect(self._on_sig_toggle)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._apply_visibility()

    def _apply_visibility(self):
        if self._sig_type == 3:
            self._c_echo.setVisible(True)
            self._c_jam.setVisible(True)
            self._c_tgt.setVisible(True)
        else:
            self._c_echo.setVisible(self._sig_type == 0)
            self._c_jam.setVisible(self._sig_type == 1)
            self._c_tgt.setVisible(self._sig_type == 2)
        self._pw.getViewBox().autoRange()

    def _on_mouse_moved(self, evt):
        pos = self._pw.getViewBox().mapSceneToView(evt)
        if self._pw.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            x, y = pos.x(), pos.y()
            self._crosshair.setText(f"t={x:.3f} us  |x|={y:.4f}")
            self._crosshair.setPos(x, y)
            self._crosshair.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair.setVisible(False)

    def update_plot(self, t_axis, echo_col, jam_col, tgt_col):
        """所有输入均为 (nrn,) 1D 向量"""
        if t_axis is None or len(t_axis) == 0:
            return
        self._t_axis = t_axis
        t_us = t_axis * 1e6
        self._c_echo.setData(t_us, np.abs(echo_col) if echo_col is not None else [])
        self._c_jam.setData(t_us, np.abs(jam_col) if jam_col is not None else [])
        self._c_tgt.setData(t_us, np.abs(tgt_col) if tgt_col is not None else [])
        self._apply_visibility()

    def clear_data(self):
        self._c_echo.setData([], [])
        self._c_jam.setData([], [])
        self._c_tgt.setData([], [])
        self._t_axis = None


# ── 6. 分离时频 (scipy STFT) ──

class SeparatedSTFTPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号选择
        ctrl_row, self._sig_group = _make_sig_buttons(
            self, "sigToggle",
            [("分离干扰", "red"), ("分离目标", "green")],
            0,
        )

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="分离时频",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "时间", units="us")
        _set_label(self._plot, "left", "频率", units="MHz")

        self._img = pg.ImageItem()
        jet_colors = [
            (0.0,  (0, 0, 143)),   (0.12, (0, 0, 255)),
            (0.25, (0, 127, 255)), (0.37, (0, 255, 255)),
            (0.5,  (0, 255, 0)),   (0.62, (255, 255, 0)),
            (0.75, (255, 127, 0)), (0.87, (255, 0, 0)),
            (1.0,  (128, 0, 0)),
        ]
        self._jet_cmap = pg.ColorMap(
            pos=[c[0] for c in jet_colors],
            color=[c[1] for c in jet_colors],
        )
        self._img.setColorMap(self._jet_cmap)
        self._plot.addItem(self._img)

        self._lut = pg.HistogramLUTItem(image=self._img)
        self._lut.gradient.setColorMap(self._jet_cmap)
        self._layout_widget.addItem(self._lut)

        layout.addLayout(ctrl_row)
        layout.addWidget(self._layout_widget, stretch=1)

        self._sig_type = 0
        self._cache = {}
        self._sig_group.idToggled.connect(self._on_sig_toggle)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._render_cached()

    def _compute_stft(self, col, fs):
        nperseg = min(64, len(col))
        noverlap = nperseg - 1
        nfft = 256
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            f_arr, t_arr, Zxx = scipy_stft(
                col, fs=fs, window='hamming', nperseg=nperseg,
                noverlap=noverlap, nfft=nfft,
                return_onesided=False,
            )
        Zxx = fftshift(Zxx, axes=0)
        f_arr = fftshift(f_arr)
        mag_db = 20.0 * np.log10(np.abs(Zxx) + 1e-12)
        return f_arr, t_arr, mag_db

    def _render_cached(self):
        keys = ["jamming", "target"]
        key = keys[self._sig_type]
        label_names = {0: "分离干扰", 1: "分离目标"}

        if key not in self._cache:
            self._img.clear()
            self._plot.setTitle(f"分离时频 — {label_names[self._sig_type]} (无数据)",
                                color=_pt()["pg_fg"], size="11pt")
            return
        f_arr, t_arr, mag_db = self._cache[key]
        peak = float(np.max(mag_db))
        vmin = peak - 50.0
        vmax = peak

        self._img.setImage(mag_db, autoLevels=False)
        self._img.setLevels([vmin, vmax])
        self._lut.setLevels(vmin, vmax)

        t_range = t_arr[-1] - t_arr[0] if len(t_arr) > 1 else 1.0
        f_range = f_arr[-1] - f_arr[0] if len(f_arr) > 1 else 1.0
        self._img.setRect(
            pg.QtCore.QRectF(
                t_arr[0] * 1e6,
                f_arr[0] / 1e6,
                t_range * 1e6,
                f_range / 1e6,
            )
        )
        self._plot.getViewBox().autoRange()
        self._plot.setTitle(f"分离时频 — {label_names[self._sig_type]}",
                            color=_pt()["pg_fg"], size="11pt")

    def update_plot(self, jam_col, tgt_col, fs):
        self._cache = {}
        if jam_col is not None and len(jam_col) > 0 and np.max(np.abs(jam_col)) > 1e-10:
            self._cache["jamming"] = self._compute_stft(jam_col, fs)
        if tgt_col is not None and len(tgt_col) > 0 and np.max(np.abs(tgt_col)) > 1e-10:
            self._cache["target"] = self._compute_stft(tgt_col, fs)
        self._render_cached()

    def clear_data(self):
        self._img.clear()
        self._cache = {}


# ── 7. 检测统计 (柱状图 + 文字) ──

class DetectionStatsPlot(QWidget):

    _TYPE_NAMES = {0: "无干扰", 1: "间歇(ISDJ/ISRJ/ISCJ)", 2: "RDJ", 3: "NBJ"}

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])

        # 柱状图
        self._bar_plot = self._layout_widget.addPlot(
            title="检测统计 — 各类型投票数",
            axisItems={'left': pg.AxisItem('left'), 'bottom': pg.AxisItem('bottom')},
        )
        self._bar_plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._bar_plot)
        self._bar_plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._bar_plot, "bottom", "识别类型")
        _set_label(self._bar_plot, "left", "投票脉冲数")

        # 预创建 4 个柱子
        self._bars = []
        self._bar_width = 0.6
        colors = ["#888888", "#5b8def", "#e07050", "#50c050"]
        for i in range(4):
            bar = pg.BarGraphItem(x=[i], height=[0], width=self._bar_width,
                                  brush=pg.mkBrush(colors[i]),
                                  pen=pg.mkPen(colors[i], width=1))
            self._bar_plot.addItem(bar)
            self._bars.append(bar)

        # 文字信息区域
        self._info_text = pg.TextItem(anchor=(0, 0), color=pt["text"])
        self._info_text.setFont(pg.QtGui.QFont("Menlo", 10))
        self._info_text.setPos(0, 0)
        self._bar_plot.addItem(self._info_text, ignoreBounds=True)

        layout.addWidget(self._layout_widget, stretch=1)

        self._setup_x_axis()

    def _setup_x_axis(self):
        # 设置 X 轴范围和刻度
        self._bar_plot.setXRange(-0.5, 3.5, padding=0.1)
        ax = self._bar_plot.getAxis('bottom')
        ticks = [(i, self._TYPE_NAMES[i]) for i in range(4)]
        ax.setTicks([ticks])

    def update_plot(self, detection_types, dominant_type, correct_count, cpiNum, isr, elapsed):
        """detection_types: (cpiNum,) int array"""
        if detection_types is None:
            return

        # 统计各类型投票数
        counts = [0, 0, 0, 0]
        for t in detection_types:
            if 0 <= t <= 3:
                counts[t] += 1

        # 删除旧柱子, 重新创建
        for bar in self._bars:
            self._bar_plot.removeItem(bar)
        self._bars = []

        colors = ["#888888", "#5b8def", "#e07050", "#50c050"]
        max_count = max(counts) if max(counts) > 0 else 1
        for i in range(4):
            if i == dominant_type:
                brush = pg.mkBrush(colors[i])
                pen = pg.mkPen("#FFD700", width=2)
            else:
                c = QColor(colors[i])
                c.setAlpha(100)
                brush = pg.mkBrush(c)
                pen = pg.mkPen(colors[i], width=1)
            bar = pg.BarGraphItem(x=[i], height=[counts[i]], width=self._bar_width,
                                  brush=brush, pen=pen)
            self._bar_plot.addItem(bar)
            self._bars.append(bar)

        self._bar_plot.setYRange(0, max_count * 1.15, padding=0.05)
        self._bar_plot.getViewBox().update()

        # 信息文字
        type_name = self._TYPE_NAMES.get(dominant_type, f"未知({dominant_type})")
        info = (
            f"识别结果: {dominant_type} ({type_name})\n"
            f"投票数: {correct_count}/{cpiNum}\n"
            f"ISR (干扰抑制比): {isr:.1f} dB\n"
            f"  = 20·lg((max|目标分离|/max|干扰分离|)\n"
            f"         /(max|含噪目标|/max|含干扰回波|))\n"
            f"耗时: {elapsed*1000:.1f} ms"
        )
        self._info_text.setText(info)
        # 放在右上角
        self._info_text.setPos(3.5, max_count * 1.05)

        self._bar_plot.setTitle(
            f"检测统计 — 识别为 {type_name} (正确 {correct_count}/{cpiNum})",
            color=_pt()["pg_fg"], size="11pt",
        )

    def clear_data(self):
        for bar in self._bars:
            self._bar_plot.removeItem(bar)
        self._bars = []
        self._info_text.setText("")
        self._bar_plot.setTitle("检测统计 — 各类型投票数",
                                color=_pt()["pg_fg"], size="11pt")


# ── PlotPanel 主面板 ──

class PlotPanel(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._results = {}   # {label: result_dict}
        self._result = None
        self._current_label = None
        self._fs = 120e6
        self._pending_cpi = None
        self._cpi_timer = QTimer(self)
        self._cpi_timer.setSingleShot(True)
        self._cpi_timer.setInterval(30)
        self._cpi_timer.timeout.connect(self._do_update_cpi)
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)

        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        lbl_type = QLabel("干扰类型:")
        lbl_type.setStyleSheet(f"color: {_pt()['text']}; font-size: 12px; font-weight: bold;")
        toolbar.addWidget(lbl_type)

        self._type_combo = QComboBox()
        self._type_combo.setMinimumWidth(220)
        self._type_combo.currentIndexChanged.connect(self._on_type_changed)
        toolbar.addWidget(self._type_combo)

        lbl_cpi = QLabel("脉冲序号:")
        lbl_cpi.setStyleSheet(f"color: {_pt()['text']}; font-size: 12px; font-weight: bold;")
        toolbar.addWidget(lbl_cpi)

        self._cpi_spin = QSpinBox()
        self._cpi_spin.setRange(0, 0)
        self._cpi_spin.setMinimumWidth(80)
        self._cpi_spin.valueChanged.connect(self._on_cpi_changed)
        toolbar.addWidget(self._cpi_spin)

        toolbar.addStretch()

        export_btn = QPushButton("导出图片")
        export_btn.setObjectName("smallButton")
        export_btn.setFixedWidth(80)
        export_btn.clicked.connect(self._export)

        export_data_btn = QPushButton("导出数据")
        export_data_btn.setObjectName("smallButton")
        export_data_btn.setFixedWidth(80)
        export_data_btn.clicked.connect(self._export_data)

        toolbar.addWidget(export_btn)
        toolbar.addWidget(export_data_btn)

        layout.addLayout(toolbar)

        self._tabs = QTabWidget()

        self._time_plot = TimeDomainPlot()
        self._freq_plot = FreqDomainPlot()
        self._stft_plot = STFTPlotWidget()
        self._mask_plot = JamMaskPlotWidget()
        self._sep_plot = SeparationPlot()
        self._sep_stft_plot = SeparatedSTFTPlot()
        self._stats_plot = DetectionStatsPlot()

        self._tabs.addTab(self._time_plot, " 时域波形 ")
        self._tabs.addTab(self._freq_plot, " 频域频谱 ")
        self._tabs.addTab(self._stft_plot, " 含干扰回波STFT ")
        self._tabs.addTab(self._mask_plot, " 干扰定位 ")
        self._tabs.addTab(self._sep_plot, " 分离对比 ")
        self._tabs.addTab(self._sep_stft_plot, " 分离时频 ")
        self._tabs.addTab(self._stats_plot, " 检测统计 ")
        self._tabs.currentChanged.connect(self._on_tab_changed)

        layout.addWidget(self._tabs)

    def _on_tab_changed(self, idx):
        if self._result is not None:
            cpi_idx = self._cpi_spin.value()
            if cpi_idx >= 0:
                self._force_update_visible(idx, cpi_idx)

    def _force_update_visible(self, tab_idx, cpi_idx):
        r = self._result
        if r is None:
            return

        # 不依赖 echo_signal 的 tab 直接处理
        if tab_idx == 2:
            self._stft_plot.update_plot(r.get("stft_matrix"))
            return
        elif tab_idx == 3:
            self._mask_plot.update_plot(r.get("stft_matrix"), r.get("jam_mask"))
            return
        elif tab_idx == 6:
            self._stats_plot.update_plot(
                r.get("detection_types"), r.get("dominant_type", 0),
                r.get("correct_count", 0), r.get("cpiNum", 0),
                r.get("isr", 0.0), r.get("elapsed", 0.0),
            )
            return

        # 以下 tab 需要 echo_signal
        echo = r.get("echo_signal")
        if echo is None or cpi_idx >= echo.shape[0]:
            return

        t_axis = np.arange(r["nrn"]) / self._fs
        echo_col = echo[cpi_idx]
        noise_col = r.get("s_echo_noise")
        jam_col = r.get("jamming_signal")
        tgt_col = r.get("target_signal")
        fr = np.fft.fftshift(np.fft.fftfreq(r["nrn"], 1.0 / self._fs))

        cols = [echo_col, noise_col, jam_col, tgt_col]

        if tab_idx == 0:
            self._time_plot.update_plot(t_axis, cols)
        elif tab_idx == 1:
            self._freq_plot.update_plot(fr, cols)
        elif tab_idx == 4:
            # 分离仅对 CPI 0 执行 (C++ jamTarDivi 仅处理 echo_signal.row(0))
            self._sep_plot.update_plot(t_axis, echo[0], jam_col, tgt_col)
        elif tab_idx == 5:
            self._sep_stft_plot.update_plot(jam_col, tgt_col, self._fs)

    _TYPE_NAMES = {
        1: "Label 1: ISDJ 间歇采样直接转发",
        2: "Label 2: ISRJ 间歇采样重复转发",
        3: "Label 3: ISCJ 间歇采样循环转发",
        4: "Label 4: NBJ 窄带噪声",
        5: "Label 5: RDJ 距离欺骗干扰",
    }

    def update_all_results(self, results):
        """一次性设置所有结果 (全选运行后调用)"""
        self._results.update(results)
        self._rebuild_type_combo()

    def update_plots(self, label, result):
        """追加单次结果到已有集合 (支持多次运行累积)"""
        self._results[label] = result
        self._rebuild_type_combo()

    def _rebuild_type_combo(self):
        """重建干扰类型 combo, 保持当前选中"""
        self._type_combo.blockSignals(True)
        prev_label = self._current_label
        self._type_combo.clear()
        for lbl in sorted(self._results):
            self._type_combo.addItem(
                self._TYPE_NAMES.get(lbl, f"Label {lbl}"), userData=lbl
            )
        # 保持之前选中, 没有则选最后一个
        target_idx = self._type_combo.count() - 1
        for i in range(self._type_combo.count()):
            if self._type_combo.itemData(i) == prev_label:
                target_idx = i
                break
        self._type_combo.setCurrentIndex(target_idx)
        self._type_combo.blockSignals(False)

        self._switch_to_label(self._type_combo.itemData(target_idx))

    def _on_type_changed(self, idx):
        if idx < 0 or not self._results:
            return
        label = self._type_combo.itemData(idx)
        if label is not None:
            self._switch_to_label(label)

    def _switch_to_label(self, label):
        result = self._results.get(label)
        if result is None:
            return
        self._current_label = label
        self._result = result

        # 更新 CPI 范围
        cpiNum = result.get("cpiNum", 0)
        echo = result.get("echo_signal")
        if echo is not None:
            cpiNum = echo.shape[0]
        self._cpi_spin.blockSignals(True)
        self._cpi_spin.setRange(0, max(cpiNum - 1, 0))
        self._cpi_spin.setValue(0)
        self._cpi_spin.blockSignals(False)

        self._refresh_plots()

    def refresh_current(self):
        """主题切换等场景: 不改 combo/CPI, 仅重绘当前图表"""
        self._refresh_plots()

    def _refresh_plots(self):
        r = self._result
        if r is None:
            return

        # STFT / 掩码 / 统计图不需要 CPI 选择
        self._stft_plot.update_plot(r.get("stft_matrix"))
        self._mask_plot.update_plot(r.get("stft_matrix"), r.get("jam_mask"))
        self._stats_plot.update_plot(
            r.get("detection_types"), r.get("dominant_type", 0),
            r.get("correct_count", 0), r.get("cpiNum", 0),
            r.get("isr", 0.0), r.get("elapsed", 0.0),
        )

        # 更新当前选中的 CPI
        cpi_idx = self._cpi_spin.value()
        if cpi_idx >= 0:
            self._update_single_cpi(cpi_idx)

    def _on_cpi_changed(self, idx):
        if idx < 0 or self._result is None:
            return
        self._pending_cpi = idx
        self._cpi_timer.start()

    def _do_update_cpi(self):
        if self._pending_cpi is not None:
            self._update_single_cpi(self._pending_cpi)
            self._pending_cpi = None

    def _update_single_cpi(self, cpi_idx):
        r = self._result
        if r is None:
            return
        echo = r.get("echo_signal")
        if echo is None or cpi_idx >= echo.shape[0]:
            return

        t_axis = np.arange(r["nrn"]) / self._fs
        echo_col = echo[cpi_idx]
        noise_col = r.get("s_echo_noise")
        jam_col = r.get("jamming_signal")
        tgt_col = r.get("target_signal")
        fr = np.fft.fftshift(np.fft.fftfreq(r["nrn"], 1.0 / self._fs))

        cols = [echo_col, noise_col, jam_col, tgt_col]

        current_tab = self._tabs.currentIndex()

        if current_tab == 0:
            self._time_plot.update_plot(t_axis, cols)
        elif current_tab == 1:
            self._freq_plot.update_plot(fr, cols)
        elif current_tab == 4:
            # 分离仅对 CPI 0 执行 (C++ jamTarDivi 仅处理 echo_signal.row(0))
            self._sep_plot.update_plot(t_axis, echo[0], jam_col, tgt_col)
        elif current_tab == 5:
            self._sep_stft_plot.update_plot(jam_col, tgt_col, self._fs)

    def clear_plots(self):
        for w in [self._time_plot, self._freq_plot, self._stft_plot,
                   self._mask_plot, self._sep_plot, self._sep_stft_plot,
                   self._stats_plot]:
            w.clear_data()
        self._cpi_spin.setRange(0, 0)
        self._cpi_spin.setValue(0)
        self._type_combo.clear()
        self._results = {}
        self._result = None
        self._current_label = None
        self._cpi_timer.stop()
        self._pending_cpi = None

    def _export(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "导出图片", "plot.png",
            "PNG 图片 (*.png);;SVG 矢量图 (*.svg);;所有文件 (*)"
        )
        if not path:
            return
        try:
            widget = self._tabs.currentWidget()
            if widget is None:
                return

            ext = path.rsplit(".", 1)[-1].lower() if "." in path else "png"

            if ext == "svg":
                from pyqtgraph.exporters import SVGExporter
                if isinstance(widget, (STFTPlotWidget, JamMaskPlotWidget, SeparatedSTFTPlot)):
                    exporter = SVGExporter(widget._plot)
                elif isinstance(widget, DetectionStatsPlot):
                    exporter = SVGExporter(widget._bar_plot)
                else:
                    exporter = SVGExporter(widget._pw.plotItem)
                exporter.export(path)
            else:
                from pyqtgraph.exporters import ImageExporter
                if isinstance(widget, (STFTPlotWidget, JamMaskPlotWidget, SeparatedSTFTPlot)):
                    exporter = ImageExporter(widget._plot)
                elif isinstance(widget, DetectionStatsPlot):
                    exporter = ImageExporter(widget._bar_plot)
                else:
                    exporter = ImageExporter(widget._pw.plotItem)
                exporter.parameters()['width'] = 1920
                exporter.export(path)

            QMessageBox.information(self, "导出成功", f"图片已保存到:\n{path}")
        except Exception as e:
            QMessageBox.warning(self, "导出失败", str(e))

    def _export_data(self):
        if not self._result:
            QMessageBox.information(self, "提示", "请先运行检测")
            return

        dir_path = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not dir_path:
            return

        saved = []
        try:
            r = self._result

            # echo_signal (cpiNum x nrn)
            echo = r.get("echo_signal")
            if echo is not None:
                path = os.path.join(dir_path, "03_detection_echo_signal.dat")
                self._save_complex_matrix(echo, path)
                saved.append(os.path.basename(path))

            # jamming_signal (nrn,)
            jam = r.get("jamming_signal")
            if jam is not None:
                path = os.path.join(dir_path, "03_detection_jamming_signal.dat")
                self._save_complex_vector(jam, path)
                saved.append(os.path.basename(path))

            # target_signal (nrn,)
            tgt = r.get("target_signal")
            if tgt is not None:
                path = os.path.join(dir_path, "03_detection_target_signal.dat")
                self._save_complex_vector(tgt, path)
                saved.append(os.path.basename(path))

            # detection_types (cpiNum,)
            dtypes = r.get("detection_types")
            if dtypes is not None:
                path = os.path.join(dir_path, "03_detection_types.txt")
                np.savetxt(path, dtypes, fmt="%d", header="detection_types (cpiNum)")
                saved.append(os.path.basename(path))

            # stft_matrix
            stft = r.get("stft_matrix")
            if stft is not None:
                path = os.path.join(dir_path, "03_detection_stft_matrix.dat")
                self._save_complex_matrix(stft, path)
                saved.append(os.path.basename(path))

            msg = "\n".join(saved)
            QMessageBox.information(self, "导出成功", f"已导出 {len(saved)} 个文件:\n{msg}")
        except Exception as e:
            QMessageBox.warning(self, "导出失败", str(e))

    @staticmethod
    def _save_complex_matrix(mat, path):
        nr, nc = mat.shape
        with open(path, "w") as f:
            f.write(f"{nr} {nc}\n")
            for i in range(nr):
                for j in range(nc):
                    v = mat[i, j]
                    f.write(f"( {v.real:.15e} + {v.imag:.15e}j )\n")

    @staticmethod
    def _save_complex_vector(vec, path):
        n = len(vec)
        with open(path, "w") as f:
            f.write(f"{n}\n")
            for i in range(n):
                v = vec[i]
                f.write(f"( {v.real:.15e} + {v.imag:.15e}j )\n")
