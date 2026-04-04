"""
增强版可视化面板 — 干扰生成 GUI (7种图表)

1. 时域波形 — 目标/干扰/合成信号 (实部/虚部/包络)
2. 频域频谱 — FFT 幅度 (dB)
3. 信号矩阵 — 热力图 (三种信号切换)
4. 距离-多普勒 — 2D RD 图 (range-FFT + azimuth-FFT)
5. STFT 时频 — 时频分析图
6. 脉冲对比 — 目标/干扰/合成三线叠加
7. 拖引轨迹 — 距离/速度 vs 脉冲索引 (仅 Case 5/6)
"""

import os
import sys
import numpy as np
import warnings
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTabWidget,
    QLabel, QPushButton, QComboBox, QFileDialog,
    QMessageBox, QButtonGroup,
)
from PySide6.QtCore import Qt, QTimer

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


_AXIS_FONT = pg.QtGui.QFont("PingFang SC", 11)
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


class _BasePlot(pg.PlotWidget):

    def __init__(self, title="", parent=None):
        super().__init__(parent, axisItems={'left': pg.AxisItem('left')})
        self.setTitle(title, color=_pt()["pg_fg"], size="11pt")
        self.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self)
        self.getViewBox().setBackgroundColor(_pt()["viewbox_bg"])
        self.getViewBox().enableAutoRange(True)
        self._legend = None

        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self.addItem(self._vline, ignoreBounds=True)
        self.addItem(self._hline, ignoreBounds=True)

        self._crosshair_label = pg.TextItem(anchor=(0, 0), color=_pt()["pg_fg"],
                                             fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair_label.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair_label.setPos(0, 0)
        self._crosshair_label.setVisible(False)
        self._crosshair_label.setZValue(999)
        self.addItem(self._crosshair_label, ignoreBounds=True)

        self.scene().sigMouseMoved.connect(self._on_mouse_moved)

    def _on_mouse_moved(self, evt):
        pos = self.getViewBox().mapSceneToView(evt)
        if self.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            self._format_crosshair(pos)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair_label.setVisible(False)

    def _format_crosshair(self, pos):
        x, y = pos.x(), pos.y()
        self._crosshair_label.setText(f"x={x:.4e}  y={y:.4e}")
        self._crosshair_label.setPos(x, y)
        self._crosshair_label.setVisible(True)

    def _make_legend(self):
        if self._legend is None:
            self._legend = self.addLegend(
                offset=(10, 10), labelTextColor=_pt()["text"],
                brush=pg.mkBrush(255, 255, 255, 220),
                pen=pg.mkPen(_pt()["axis"]),
            )
        return self._legend

    def _add_legend(self, name, item):
        lg = self._make_legend()
        lg.addItem(item, name)

    def clear_data(self):
        for item in self.items():
            if isinstance(item, pg.PlotDataItem):
                item.setData([], [])
            elif isinstance(item, pg.ScatterPlotItem):
                item.setData([], [])


# ── 1. 时域波形 ──

class TimeDomainPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号类型选择 + 波形类型选择
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(8)

        # 信号类型: 目标/干扰/合成
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("目标回波", "blue"), ("干扰信号", "red"), ("合成回波", "green"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("sigToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 2)
            btn.setStyleSheet(
                f"QPushButton[sigToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[sigToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._sig_group.addButton(btn, idx)
            ctrl_row.addWidget(btn)

        ctrl_row.addSpacing(20)

        # 波形类型: 实部/虚部/包络
        self._wave_group = QButtonGroup(self)
        self._wave_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("实部", "blue"), ("虚部", "red"), ("包络", "green"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("waveToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 2)
            btn.setStyleSheet(
                f"QPushButton[waveToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[waveToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._wave_group.addButton(btn, idx)
            ctrl_row.addWidget(btn)

        ctrl_row.addStretch()

        # PlotWidget
        self._pw = pg.PlotWidget(axisItems={'left': pg.AxisItem('left')})
        self._pw.setTitle("时域波形", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "快时间", units="s")
        _set_label(self._pw, "left", "幅度")

        self._curve = self._pw.plot(pen=pg.mkPen(pt["green"], width=1.5),
                                    clipToView=True, downsample=2)

        # 十字准线
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._pw.addItem(self._vline, ignoreBounds=True)
        self._pw.addItem(self._hline, ignoreBounds=True)
        self._crosshair_label = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                             fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair_label.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair_label.setPos(0, 0)
        self._crosshair_label.setVisible(False)
        self._pw.addItem(self._crosshair_label, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        layout.addLayout(ctrl_row)
        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 2  # 0=target, 1=jam, 2=echo
        self._wave_type = 2  # 0=real, 1=imag, 2=envelope
        self._tnrn = None
        self._cols = [None, None, None]  # target, jam, echo

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
            self._crosshair_label.setText(f"t={x:.4e} s  amp={y:.4f}")
            self._crosshair_label.setPos(x, y)
            self._crosshair_label.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair_label.setVisible(False)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._apply_data()

    def _on_wave_toggle(self, btn_id, checked):
        if checked:
            self._wave_type = btn_id
            self._apply_data()

    def _apply_data(self):
        if self._tnrn is None:
            return
        col = self._cols[self._sig_type]
        if col is None:
            return
        self._update_curve(self._tnrn, col)
        self._pw.getViewBox().autoRange()

    def _update_curve(self, tnrn, col):
        if self._wave_type == 0:
            self._curve.setData(tnrn, col.real)
        elif self._wave_type == 1:
            self._curve.setData(tnrn, col.imag)
        else:
            self._curve.setData(tnrn, np.abs(col))

        sig_names = {0: "目标回波", 1: "干扰信号", 2: "合成回波"}
        wave_names = {0: "实部", 1: "虚部", 2: "包络"}
        self._pw.setTitle(f"时域波形 — {sig_names[self._sig_type]} ({wave_names[self._wave_type]})",
                          color=_pt()["pg_fg"], size="11pt")

    def update_plot(self, tnrn, target_col, jam_col, echo_col):
        """接收三种信号的单脉冲列, 根据 _sig_type 显示."""
        if tnrn is None or len(tnrn) == 0:
            return
        self._tnrn = tnrn
        self._cols = [target_col, jam_col, echo_col]
        col = self._cols[self._sig_type]
        if col is not None:
            self._update_curve(tnrn, col)
        self._pw.getViewBox().autoRange()

    def clear_data(self):
        self._curve.setData([], [])
        self._tnrn = None
        self._cols = [None, None, None]


# ── 2. 频域频谱 ──

class FreqDomainPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号类型选择
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(4)
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("目标回波", "blue"), ("干扰信号", "red"), ("合成回波", "green"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("sigToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 2)
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
        self._crosshair_label = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                             fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair_label.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair_label.setPos(0, 0)
        self._crosshair_label.setVisible(False)
        self._pw.addItem(self._crosshair_label, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 2
        self._fr = None
        self._cols = [None, None, None]  # target, jam, echo

        self._sig_group.idToggled.connect(self._on_sig_toggle)

    def _on_mouse_moved(self, evt):
        pos = self._pw.getViewBox().mapSceneToView(evt)
        if self._pw.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            x, y = pos.x(), pos.y()
            self._crosshair_label.setText(f"f={x:.2f} MHz  |H|={y:.1f} dB")
            self._crosshair_label.setPos(x, y)
            self._crosshair_label.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair_label.setVisible(False)

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
        self._compute_spectrum(self._fr, col)

    def _compute_spectrum(self, fr, col):
        spec = fftshift(fft(col))
        mag = 20.0 * np.log10(np.abs(spec) + 1e-12)
        self._curve.setData(fr / 1e6, mag)
        sig_names = {0: "目标回波", 1: "干扰信号", 2: "合成回波"}
        self._pw.setTitle(f"频谱 — {sig_names[self._sig_type]}",
                          color=_pt()["pg_fg"], size="11pt")
        self._pw.getViewBox().autoRange()

    def update_plot(self, fr, target_col, jam_col, echo_col):
        if fr is None or len(fr) == 0:
            return
        self._fr = fr
        self._cols = [target_col, jam_col, echo_col]
        col = self._cols[self._sig_type]
        if col is not None:
            self._compute_spectrum(fr, col)

    def clear_data(self):
        self._curve.setData([], [])
        self._fr = None
        self._cols = [None, None, None]


# ── 3. 信号矩阵热力图 ──

class ImagePlotWidget(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="信号矩阵热力图",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "脉冲索引")
        _set_label(self._plot, "left", "距离单元")

        self._img = pg.ImageItem()
        self._img.setColorMap(pg.colormap.get("viridis"))
        self._plot.addItem(self._img)

        self._lut = pg.HistogramLUTItem(image=self._img)
        self._layout_widget.addItem(self._lut)

        # 信号类型 + 显示模式
        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)

        # 信号类型: 目标/干扰/合成
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("目标回波", "blue"), ("干扰信号", "red"), ("合成回波", "green"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("sigToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 2)
            btn.setStyleSheet(
                f"QPushButton[sigToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[sigToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._sig_group.addButton(btn, idx)
            btn_row.addWidget(btn)

        btn_row.addSpacing(10)

        # 显示模式: 实部/虚部/幅度/相位
        self._mode_group = QButtonGroup(self)
        self._mode_group.setExclusive(True)
        for name, idx, color_key in [("实部", 0, "blue"), ("虚部", 1, "red"),
                                      ("幅度", 2, "green"), ("相位", 3, "purple")]:
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("imgToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 2)
            btn.setStyleSheet(
                f"QPushButton[imgToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[imgToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._mode_group.addButton(btn, idx)
            btn_row.addWidget(btn)

        btn_row.addStretch()

        self._sig_type = 2  # 0=target, 1=jam, 2=echo
        self._display_mode = 2  # 0=real, 1=imag, 2=amp, 3=phase
        self._sig_matrices = {}  # type -> matrix
        self._sig_group.idToggled.connect(self._on_sig_toggle)
        self._mode_group.idToggled.connect(self._on_mode_toggle)

        outer.addWidget(self._layout_widget, stretch=1)
        outer.addLayout(btn_row)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._render()

    def _on_mode_toggle(self, btn_id, checked):
        if checked:
            self._display_mode = btn_id
            self._render()

    def _current_matrix(self):
        sig_names = {0: "target_signal", 1: "jam_signal", 2: "echo_target"}
        return self._sig_matrices.get(sig_names[self._sig_type])

    def _render(self):
        sig = self._current_matrix()
        if sig is None:
            return

        if self._display_mode == 0:
            data = sig.real[::-1]
            title_prefix = "实部"
            cmap = "viridis"
        elif self._display_mode == 1:
            data = sig.imag[::-1]
            title_prefix = "虚部"
            cmap = "viridis"
        elif self._display_mode == 2:
            data = np.abs(sig)[::-1]
            title_prefix = "幅度"
            cmap = "viridis"
        else:
            data = np.angle(sig)[::-1]
            title_prefix = "相位 (rad)"
            cmap = "CET-D1A"

        sig_names = {0: "目标回波", 1: "干扰信号", 2: "合成回波"}
        self._img.setColorMap(pg.colormap.get(cmap))
        vmin = float(np.min(data))
        vmax = float(np.max(data))
        if vmin >= vmax:
            vmax = vmin + 1
        self._img.setImage(data, autoLevels=False)
        self._img.setLevels([vmin, vmax])
        nr, nc = sig.shape
        self._img.setRect(pg.QtCore.QRectF(0, 0, nc, nr))
        self._plot.setTitle(f"信号矩阵 — {sig_names[self._sig_type]} ({title_prefix})")
        _set_label(self._plot, "bottom", "脉冲索引", units=f"  [0~{nc - 1}]")
        _set_label(self._plot, "left", "距离单元", units=f"  [0~{nr - 1}]")
        if not hasattr(self, '_sig_shape') or self._sig_shape != sig.shape:
            self._sig_shape = sig.shape
            self._plot.getViewBox().setLimits(
                xMin=-0.5, xMax=nc - 0.5,
                yMin=-0.5, yMax=nr - 0.5,
            )
            self._plot.getViewBox().setRange(
                xRange=(-0.5, nc - 0.5),
                yRange=(-0.5, nr - 0.5),
                padding=0.02,
            )

    def update_plot(self, target_signal, jam_signal, echo_target):
        if echo_target is not None:
            self._sig_matrices["echo_target"] = echo_target
        if jam_signal is not None:
            self._sig_matrices["jam_signal"] = jam_signal
        if target_signal is not None:
            self._sig_matrices["target_signal"] = target_signal
        self._render()

    def clear_data(self):
        self._img.clear()
        self._sig_matrices = {}


# ── 4. 距离-多普勒图 ──

class RangeDopplerPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="距离-多普勒图",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "多普勒单元")
        _set_label(self._plot, "left", "距离单元")

        self._img = pg.ImageItem()
        # jet 彩虹色标: 蓝(低)→青→绿→黄→红(高), 雷达 RD 图标配
        jet_colors = [
            (0.0,  (0, 0, 143)),
            (0.12, (0, 0, 255)),
            (0.25, (0, 127, 255)),
            (0.37, (0, 255, 255)),
            (0.5,  (0, 255, 0)),
            (0.62, (255, 255, 0)),
            (0.75, (255, 127, 0)),
            (0.87, (255, 0, 0)),
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

        # 信号类型选择
        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("合成回波", "green"), ("目标回波", "blue"), ("干扰信号", "red"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("sigToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 0)
            btn.setStyleSheet(
                f"QPushButton[sigToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[sigToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._sig_group.addButton(btn, idx)
            btn_row.addWidget(btn)
        btn_row.addStretch()

        self._sig_type = 0  # 0=echo, 1=target, 2=jam
        self._rd_cache = {}
        self._tnrn = None
        self._gama = 0.0
        self._fs = 0.0
        self._fc = 0.0
        self._prf = 10e3
        self._sig_group.idToggled.connect(self._on_sig_toggle)

        layout.addWidget(self._layout_widget, stretch=1)
        layout.addLayout(btn_row)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._render_cached()

    def _compute_rd(self, matrix):
        """计算 Range-Doppler 图: 去斜 → 距离向 FFT+fftshift → 多普勒向 FFT+fftshift."""
        nrn, nan1 = matrix.shape
        # 去斜 (de-chirp): 乘以参考 chirp 的共轭
        if self._tnrn is not None and self._gama > 0:
            ref = np.exp(1j * np.pi * self._gama * self._tnrn ** 2)
            dechirped = matrix * np.conj(ref)[:, np.newaxis]
        else:
            dechirped = matrix
        # 距离向脉压 (每列 FFT) + fftshift 使零距离居中
        range_compressed = np.fft.fftshift(np.fft.fft(dechirped, axis=0), axes=0)
        # 多普勒处理 (每行 FFT, shift 使零多普勒居中)
        rd_map = np.fft.fftshift(np.fft.fft(range_compressed, axis=1), axes=1)
        rd_mag = 20.0 * np.log10(np.abs(rd_map) + 1e-12)
        return rd_mag

    def _render_cached(self):
        sig_names = {0: "echo_target", 1: "target_signal", 2: "jam_signal"}
        label_names = {0: "合成回波", 1: "目标回波", 2: "干扰信号"}
        key = sig_names[self._sig_type]
        if key not in self._rd_cache:
            return
        rd_mag = self._rd_cache[key]
        nr, nc = rd_mag.shape

        peak = float(np.max(rd_mag))
        vmin = peak - 60.0
        vmax = peak

        self._img.setImage(rd_mag[::-1], autoLevels=False)
        self._img.setLevels([vmin, vmax])
        self._lut.setLevels(vmin, vmax)

        # 物理坐标轴
        c_light = 3e8
        if self._fs > 0 and self._gama > 0:
            xi = np.fft.fftshift(np.fft.fftfreq(nr, 1.0 / self._fs)) * c_light / (2.0 * self._gama)
        else:
            xi = np.arange(nr, dtype=float)
        if self._fc > 0 and self._prf > 0:
            lambda_ = c_light / self._fc
            dv = np.fft.fftshift(np.fft.fftfreq(nc, 1.0 / self._prf)) * lambda_ / 2.0
        else:
            dv = np.arange(nc, dtype=float)

        self._img.setRect(pg.QtCore.QRectF(dv[0], xi[0], dv[-1] - dv[0], xi[-1] - xi[0]))

        self._plot.setTitle(f"距离-多普勒图 — {label_names[self._sig_type]}",
                            color=_pt()["pg_fg"], size="11pt")
        _set_label(self._plot, "bottom", "速度", units="m/s")
        _set_label(self._plot, "left", "距离", units="m")
        self._plot.getViewBox().setLimits(
            xMin=dv[0], xMax=dv[-1],
            yMin=xi[0], yMax=xi[-1],
        )
        self._plot.getViewBox().setRange(
            xRange=(dv[0], dv[-1]),
            yRange=(xi[0], xi[-1]),
            padding=0.02,
        )

    def update_plot(self, target_signal, jam_signal, echo_target, tnrn=None, gama=0.0,
                    fs=0.0, fc=0.0, prf=10e3):
        self._tnrn = tnrn
        self._gama = gama
        self._fs = fs
        self._fc = fc
        self._prf = prf
        self._rd_cache = {}
        if echo_target is not None:
            self._rd_cache["echo_target"] = self._compute_rd(echo_target)
        if target_signal is not None:
            self._rd_cache["target_signal"] = self._compute_rd(target_signal)
        if jam_signal is not None:
            self._rd_cache["jam_signal"] = self._compute_rd(jam_signal)
        self._render_cached()

    def clear_data(self):
        self._img.clear()
        self._rd_cache = {}


# ── 5. STFT 时频分析 ──

class STFTPlotWidget(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号类型选择
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(4)
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("目标回波", "blue"), ("干扰信号", "red"), ("合成回波", "green"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("sigToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 2)
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

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="STFT 时频分析",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "时间", units="us")
        _set_label(self._plot, "left", "频率", units="MHz")

        self._img = pg.ImageItem()
        jet_colors = [
            (0.0,  (0, 0, 143)),
            (0.12, (0, 0, 255)),
            (0.25, (0, 127, 255)),
            (0.37, (0, 255, 255)),
            (0.5,  (0, 255, 0)),
            (0.62, (255, 255, 0)),
            (0.75, (255, 127, 0)),
            (0.87, (255, 0, 0)),
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

        layout.addWidget(self._layout_widget)

        self._sig_type = 2
        self._cache_key = None
        self._stft_cache = {}
        self._empty_signals: set = set()
        self._sig_group.idToggled.connect(self._on_sig_toggle)

        # 空信号提示文字
        self._empty_text = pg.TextItem(anchor=(0.5, 0.5))
        self._empty_text.setColor((180, 180, 180, 200))
        self._empty_text.setFont(pg.QtGui.QFont("Microsoft YaHei", 13, pg.QtGui.QFont.Weight.Bold))
        self._plot.addItem(self._empty_text)
        self._empty_text.setVisible(False)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._render_cached()

    def _compute_stft(self, col, fs, fc):
        if fc > 0 and fs > 0:
            n = np.arange(len(col))
            carrier_phase = 2.0 * np.pi * (fc / fs) * n
            col_bb = col * np.exp(-1j * carrier_phase)
        else:
            col_bb = col

        nperseg = min(64, len(col_bb))
        noverlap = nperseg - 1
        nfft = 256
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            f_arr, t_arr, Zxx = scipy_stft(
                col_bb, fs=fs, window='hamming', nperseg=nperseg,
                noverlap=noverlap, nfft=nfft,
                return_onesided=False,
            )
        Zxx = fftshift(Zxx, axes=0)
        f_arr = fftshift(f_arr)
        mag_db = 20.0 * np.log10(np.abs(Zxx) + 1e-12)
        return f_arr, t_arr, mag_db

    def _render_cached(self):
        keys = ["target_signal", "jam_signal", "echo_target"]
        key = keys[self._sig_type]
        label_names = {0: "目标回波", 1: "干扰信号", 2: "合成回波"}

        # 空信号提示
        if key in self._empty_signals:
            self._img.clear()
            self._empty_text.setText(f"该脉冲无{label_names[self._sig_type]}数据")
            vr = self._plot.viewRange()
            cx = (vr[0][0] + vr[0][1]) / 2
            cy = (vr[1][0] + vr[1][1]) / 2
            self._empty_text.setPos(cx, cy)
            self._empty_text.setVisible(True)
            self._plot.setTitle(f"STFT — {label_names[self._sig_type]} (无数据)",
                                color=_pt()["pg_fg"], size="11pt")
            return

        self._empty_text.setVisible(False)
        if key not in self._stft_cache:
            return
        f_arr, t_arr, mag_db = self._stft_cache[key]
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

        self._plot.setTitle(f"STFT — {label_names[self._sig_type]}",
                            color=_pt()["pg_fg"], size="11pt")

    def update_plot(self, tnrn, target_col, jam_col, echo_col, fs, fc=0.0):
        if tnrn is None or len(tnrn) == 0:
            return
        cache_key = (id(echo_col), fs, fc)
        if self._cache_key == cache_key and self._stft_cache:
            self._render_cached()
            return
        self._cache_key = cache_key
        self._stft_cache = {}
        self._empty_signals = set()

        cols = [
            ("target_signal", target_col),
            ("jam_signal", jam_col),
            ("echo_target", echo_col),
        ]
        for key, col in cols:
            if col is not None and len(col) > 0:
                if np.max(np.abs(col)) < 1e-10:
                    self._empty_signals.add(key)
                else:
                    self._stft_cache[key] = self._compute_stft(col, fs, fc)

        self._render_cached()

    def clear_data(self):
        self._img.clear()
        self._cache_key = None
        self._stft_cache = {}
        self._empty_signals = set()
        self._empty_text.setVisible(False)


# ── 6. 脉冲对比图 ──

class PulseComparePlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        # 信号类型选择: 目标/干扰/合成/全部叠加
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(4)
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("目标回波", "blue"), ("干扰信号", "red"),
            ("合成回波", "green"), ("全部叠加", "purple"),
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
        self._pw.setTitle("脉冲对比", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "快时间", units="s")
        _set_label(self._pw, "left", "包络幅度")

        self._c_target = self._pw.plot(pen=pg.mkPen(pt["blue"], width=1.5), name="目标回波",
                                       clipToView=True, downsample=2)
        self._c_jam = self._pw.plot(pen=pg.mkPen(pt["red"], width=1.5), name="干扰信号",
                                    clipToView=True, downsample=2)
        self._c_echo = self._pw.plot(pen=pg.mkPen(pt["green"], width=1.5), name="合成回波",
                                     clipToView=True, downsample=2)

        lg = self._pw.addLegend(offset=(10, 10), labelTextColor=pt["text"],
                                brush=pg.mkBrush(255, 255, 255, 220),
                                pen=pg.mkPen(pt["axis"]))
        lg.addItem(self._c_target, "目标回波")
        lg.addItem(self._c_jam, "干扰信号")
        lg.addItem(self._c_echo, "合成回波")

        # 十字准线
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._pw.addItem(self._vline, ignoreBounds=True)
        self._pw.addItem(self._hline, ignoreBounds=True)
        self._crosshair_label = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                             fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair_label.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair_label.setPos(0, 0)
        self._crosshair_label.setVisible(False)
        self._pw.addItem(self._crosshair_label, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 3  # 0=target, 1=jam, 2=echo, 3=all
        self._tnrn = None
        self._cols = [None, None, None]

        self._sig_group.idToggled.connect(self._on_sig_toggle)

    def _on_mouse_moved(self, evt):
        pos = self._pw.getViewBox().mapSceneToView(evt)
        if self._pw.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            x, y = pos.x(), pos.y()
            self._crosshair_label.setText(f"t={x:.4e} s  |x|={y:.4f}")
            self._crosshair_label.setPos(x, y)
            self._crosshair_label.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair_label.setVisible(False)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._apply_visibility()

    def _apply_visibility(self):
        if self._sig_type == 3:
            self._c_target.setVisible(True)
            self._c_jam.setVisible(True)
            self._c_echo.setVisible(True)
        else:
            self._c_target.setVisible(self._sig_type == 0)
            self._c_jam.setVisible(self._sig_type == 1)
            self._c_echo.setVisible(self._sig_type == 2)
        self._pw.getViewBox().autoRange()

    def update_plot(self, tnrn, target_col, jam_col, echo_col):
        if tnrn is None or len(tnrn) == 0:
            return
        self._tnrn = tnrn
        self._cols = [target_col, jam_col, echo_col]
        self._c_target.setData(tnrn, np.abs(target_col) if target_col is not None else [])
        self._c_jam.setData(tnrn, np.abs(jam_col) if jam_col is not None else [])
        self._c_echo.setData(tnrn, np.abs(echo_col) if echo_col is not None else [])
        self._apply_visibility()

    def clear_data(self):
        self._c_target.setData([], [])
        self._c_jam.setData([], [])
        self._c_echo.setData([], [])
        self._tnrn = None
        self._cols = [None, None, None]


# ── 7. 拖引轨迹图 ──

class TrajectoryPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._pw = pg.PlotWidget(axisItems={'left': pg.AxisItem('left')})
        self._pw.setTitle("拖引轨迹 (仅 Case 5/6)", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "脉冲索引")
        _set_label(self._pw, "left", "峰值位置")

        self._curve = self._pw.plot(pen=pg.mkPen(pt["red"], width=2),
                                    clipToView=True, symbol='o', symbolSize=4, name="干扰")
        self._curve_target = self._pw.plot(pen=pg.mkPen(pt["blue"], width=1.5, style=Qt.PenStyle.DashLine),
                                           clipToView=True, symbol='s', symbolSize=3, name="目标")

        # 十字准线
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen("#999", width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._pw.addItem(self._vline, ignoreBounds=True)
        self._pw.addItem(self._hline, ignoreBounds=True)
        self._crosshair_label = pg.TextItem(anchor=(0, 1), color=pt["pg_fg"],
                                             fill=pg.mkBrush(255, 255, 255, 210))
        self._crosshair_label.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair_label.setPos(0, 0)
        self._crosshair_label.setVisible(False)
        self._pw.addItem(self._crosshair_label, ignoreBounds=True)
        self._pw.scene().sigMouseMoved.connect(self._on_mouse_moved)

        # 轨迹类型选择
        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)
        self._type_group = QButtonGroup(self)
        self._type_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("峰值距离", "red"), ("峰值速度", "orange"),
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("trajToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 0)
            btn.setStyleSheet(
                f"QPushButton[trajToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[trajToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._type_group.addButton(btn, idx)
            btn_row.addWidget(btn)
        btn_row.addStretch()

        self._placeholder_label = QLabel("此图表仅适用于 RGPO (Case 5) 或 VGPO (Case 6) 模式")
        self._placeholder_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._placeholder_label.setStyleSheet("color: #6c7086; font-size: 13px;")

        layout.addWidget(self._placeholder_label, stretch=1)
        layout.addWidget(self._pw, stretch=1)
        layout.addLayout(btn_row)

        self._show_placeholder = True
        self._traj_type = 0
        self._jam_signal = None
        self._target_signal = None
        self._tnrn = None
        self._prf = 10e3
        self._jam_distances = None
        self._target_distances = None
        self._type_group.idToggled.connect(self._on_type_toggle)

    def _on_mouse_moved(self, evt):
        pos = self._pw.getViewBox().mapSceneToView(evt)
        if self._pw.getViewBox().sceneBoundingRect().contains(evt):
            self._vline.setPos(pos.x())
            self._hline.setPos(pos.y())
            self._vline.setVisible(True)
            self._hline.setVisible(True)
            x, y = pos.x(), pos.y()
            unit = "m" if self._traj_type == 0 else "m/s"
            self._crosshair_label.setText(f"pulse={int(x)}  val={y:.1f} {unit}")
            self._crosshair_label.setPos(x, y)
            self._crosshair_label.setVisible(True)
        else:
            self._vline.setVisible(False)
            self._hline.setVisible(False)
            self._crosshair_label.setVisible(False)

    def _on_type_toggle(self, btn_id, checked):
        if checked:
            self._traj_type = btn_id
            if not self._show_placeholder and self._jam_signal is not None:
                self._render()

    def _peak_distances(self, signal):
        """从信号矩阵计算每个脉冲的峰值距离 (m)"""
        nrn, nan1 = signal.shape
        energy = np.max(np.abs(signal), axis=0)
        peaks = np.argmax(np.abs(signal), axis=0).astype(float)
        peaks[energy < 1e-10] = np.nan
        if self._tnrn is not None:
            valid = ~np.isnan(peaks)
            distances = np.full(nan1, np.nan)
            distances[valid] = self._tnrn[peaks[valid].astype(int)] * 1.5e8
            return distances
        return peaks

    def _render(self):
        if self._jam_signal is None:
            return
        nrn, nan1 = self._jam_signal.shape
        pulses = np.arange(nan1)

        if self._jam_distances is None:
            self._jam_distances = self._peak_distances(self._jam_signal)
        if self._target_distances is None and self._target_signal is not None:
            self._target_distances = self._peak_distances(self._target_signal)

        if self._traj_type == 0:
            # ── 峰值距离 ──
            self._curve.setData(pulses, self._jam_distances)
            if self._target_distances is not None:
                self._curve_target.setData(pulses, self._target_distances)
            else:
                self._curve_target.setData([], [])
            self._pw.setTitle("拖引轨迹 — 峰值距离",
                              color=_pt()["pg_fg"], size="11pt")
            _set_label(self._pw, "bottom", "脉冲索引")
            _set_label(self._pw, "left", "距离", units="m")
        else:
            # ── 峰值速度: 脉冲间距离变化率 ──
            jam_v = np.full(nan1, np.nan)
            for k in range(1, nan1):
                if not (np.isnan(self._jam_distances[k]) or np.isnan(self._jam_distances[k - 1])):
                    jam_v[k] = (self._jam_distances[k] - self._jam_distances[k - 1]) * self._prf

            self._curve.setData(pulses, jam_v)

            if self._target_distances is not None:
                tgt_v = np.full(nan1, np.nan)
                for k in range(1, nan1):
                    if not (np.isnan(self._target_distances[k]) or np.isnan(self._target_distances[k - 1])):
                        tgt_v[k] = (self._target_distances[k] - self._target_distances[k - 1]) * self._prf
                self._curve_target.setData(pulses, tgt_v)
            else:
                self._curve_target.setData([], [])

            self._pw.setTitle("拖引轨迹 — 峰值速度",
                              color=_pt()["pg_fg"], size="11pt")
            _set_label(self._pw, "bottom", "脉冲索引")
            _set_label(self._pw, "left", "速度", units="m/s")
        self._pw.getViewBox().autoRange()

    def update_plot(self, mode, jam_signal, target_signal=None, tnrn=None, prf=10e3):
        if mode not in (5, 6):
            self._show_placeholder = True
            self._placeholder_label.setVisible(True)
            self._pw.setVisible(False)
            return

        self._show_placeholder = False
        self._placeholder_label.setVisible(False)
        self._pw.setVisible(True)
        self._jam_signal = jam_signal
        self._target_signal = target_signal
        self._tnrn = tnrn
        self._prf = prf
        self._jam_distances = None
        self._target_distances = None
        self._render()

    def clear_data(self):
        self._curve.setData([], [])
        self._curve_target.setData([], [])
        self._jam_signal = None
        self._target_signal = None
        self._tnrn = None
        self._jam_distances = None
        self._target_distances = None
        self._show_placeholder = True
        self._placeholder_label.setVisible(True)
        self._pw.setVisible(False)


# ── PlotPanel 主面板 ──

class PlotPanel(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._results = {}  # mode -> result
        self._result = None
        self._current_mode = None
        self._tnrn = None
        self._fr = None
        self._fc = 0.0
        self._fs = 0.0
        self._pending_pulse = None
        self._pulse_timer = QTimer(self)
        self._pulse_timer.setSingleShot(True)
        self._pulse_timer.setInterval(30)
        self._pulse_timer.timeout.connect(self._do_update_pulse)
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)

        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        lbl_mode = QLabel("模式:")
        lbl_mode.setStyleSheet(f"color: {_pt()['text']}; font-size: 12px; font-weight: bold;")
        toolbar.addWidget(lbl_mode)

        self._mode_combo = QComboBox()
        self._mode_combo.setMinimumWidth(200)
        self._mode_combo.currentIndexChanged.connect(self._on_mode_changed)
        toolbar.addWidget(self._mode_combo)

        toolbar.addStretch()

        lbl_pulse = QLabel("脉冲:")
        lbl_pulse.setStyleSheet(f"color: {_pt()['text']}; font-size: 12px; font-weight: bold;")
        toolbar.addWidget(lbl_pulse)

        self._pulse_combo = QComboBox()
        self._pulse_combo.setMinimumWidth(100)
        self._pulse_combo.currentIndexChanged.connect(self._on_pulse_changed)
        toolbar.addWidget(self._pulse_combo)

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
        self._image_plot = ImagePlotWidget()
        self._rd_plot = RangeDopplerPlot()
        self._stft_plot = STFTPlotWidget()
        self._pulse_compare_plot = PulseComparePlot()
        self._trajectory_plot = TrajectoryPlot()

        self._tabs.addTab(self._time_plot, " 时域波形 ")
        self._tabs.addTab(self._freq_plot, " 频域频谱 ")
        self._tabs.addTab(self._image_plot, " 信号矩阵 ")
        self._tabs.addTab(self._rd_plot, " 距离-多普勒 ")
        self._tabs.addTab(self._stft_plot, " STFT时频 ")
        self._tabs.addTab(self._pulse_compare_plot, " 脉冲对比 ")
        self._tabs.addTab(self._trajectory_plot, " 拖引轨迹 ")
        self._tabs.currentChanged.connect(self._on_tab_changed)

        layout.addWidget(self._tabs)

    def _on_tab_changed(self, idx):
        if self._current_mode is not None and self._current_mode in self._results:
            pulse_idx = self._pulse_combo.currentIndex()
            if pulse_idx >= 0:
                self._force_update_visible(idx, pulse_idx)

    def _force_update_visible(self, tab_idx, pulse_idx):
        result = self._result
        if not result:
            return
        echo = result.get("echo_target")
        if echo is None:
            return
        if pulse_idx >= echo.shape[1]:
            return

        target_col = result.get("target_signal")[:, pulse_idx] if result.get("target_signal") is not None else None
        jam_col = result.get("jam_signal")[:, pulse_idx] if result.get("jam_signal") is not None else None
        echo_col = echo[:, pulse_idx]

        if tab_idx == 0 and self._tnrn is not None:
            self._time_plot.update_plot(self._tnrn, target_col, jam_col, echo_col)
        elif tab_idx == 1 and self._fr is not None:
            self._freq_plot.update_plot(self._fr, target_col, jam_col, echo_col)
        elif tab_idx == 2:
            target = result.get("target_signal")
            jam = result.get("jam_signal")
            self._image_plot.update_plot(target, jam, echo)
        elif tab_idx == 3:
            target = result.get("target_signal")
            jam = result.get("jam_signal")
            self._rd_plot.update_plot(target, jam, echo, self._tnrn, self._gama,
                                      self._fs, self._fc, self._prf)
        elif tab_idx == 4 and self._tnrn is not None and self._fs > 0:
            # Module 02 所有信号均为基带, 不需要载波去除
            self._stft_plot.update_plot(self._tnrn, target_col, jam_col, echo_col, self._fs, fc=0.0)
        elif tab_idx == 5 and self._tnrn is not None:
            self._pulse_compare_plot.update_plot(self._tnrn, target_col, jam_col, echo_col)
        elif tab_idx == 6:
            jam = result.get("jam_signal")
            target = result.get("target_signal")
            self._trajectory_plot.update_plot(self._current_mode, jam, target, self._tnrn, self._prf)

    def set_time_freq_axes(self, tnrn, fr, fc=0.0, fs=0.0, gama=0.0, prf=10e3):
        self._tnrn = tnrn
        self._fr = fr
        self._fc = fc
        self._fs = fs
        self._gama = gama
        self._prf = prf

    _CASE_NAMES = {
        1: "Case1 RDJ 距离假目标",
        2: "Case2 VDJ 速度假目标",
        3: "Case3 ISRJ 间歇采样转发",
        4: "Case4 NNJ 窄带噪声",
        5: "Case5 RGPO 距离波门拖引",
        6: "Case6 VGPO 速度波门拖引",
        7: "Case7 DRFTJ 密集假目标",
        8: "Case8 IPLESRJ 脉内前沿切片",
        9: "Case9 SMSP 频谱弥散",
        10: "Case10 COMB 梳状谱",
    }

    def update_plots(self, mode, result):
        """追加单个 mode 的结果并刷新"""
        self._results[mode] = result

        # 重建 mode combo
        self._mode_combo.blockSignals(True)
        self._mode_combo.clear()
        mode_list = sorted(self._results.keys())
        for m in mode_list:
            self._mode_combo.addItem(self._CASE_NAMES.get(m, f"Case{m}"), userData=m)
        if mode in mode_list:
            self._mode_combo.setCurrentIndex(mode_list.index(mode))
        self._mode_combo.blockSignals(False)

        self._refresh_plots(mode)

    def _on_mode_changed(self, idx):
        if idx < 0:
            return
        mode = self._mode_combo.itemData(idx)
        if mode is not None and mode in self._results:
            self._refresh_plots(mode)

    def _refresh_plots(self, mode):
        self._current_mode = mode
        if mode is None or mode not in self._results:
            return
        self._result = self._results[mode]

        echo = self._result.get("echo_target")
        jam = self._result.get("jam_signal")
        target = self._result.get("target_signal")

        if echo is not None:
            nrn, nan1 = echo.shape

            self._pulse_combo.blockSignals(True)
            self._pulse_combo.clear()
            for i in range(nan1):
                self._pulse_combo.addItem(f"脉冲 {i}", userData=i)
            default_pulse = min(32, nan1 - 1)
            self._pulse_combo.setCurrentIndex(default_pulse)
            self._pulse_combo.blockSignals(False)

            # 更新所有图表
            self._image_plot.update_plot(target, jam, echo)
            self._rd_plot.update_plot(target, jam, echo, self._tnrn, self._gama,
                                      self._fs, self._fc, self._prf)
            self._trajectory_plot.update_plot(mode, jam, target, self._tnrn, self._prf)
            self._update_single_pulse(default_pulse)

    def _on_pulse_changed(self, idx):
        if idx < 0 or self._current_mode is None or self._current_mode not in self._results:
            return
        self._pending_pulse = idx
        self._pulse_timer.start()

    def _do_update_pulse(self):
        if self._pending_pulse is not None:
            self._update_single_pulse(self._pending_pulse)
            self._pending_pulse = None

    def _update_single_pulse(self, pulse_idx):
        result = self._result
        if not result:
            return
        echo = result.get("echo_target")
        if echo is None or pulse_idx < 0 or pulse_idx >= echo.shape[1]:
            return

        target_col = result.get("target_signal")[:, pulse_idx] if result.get("target_signal") is not None else None
        jam_col = result.get("jam_signal")[:, pulse_idx] if result.get("jam_signal") is not None else None
        echo_col = echo[:, pulse_idx]

        current_tab = self._tabs.currentIndex()

        if current_tab == 0 and self._tnrn is not None:
            self._time_plot.update_plot(self._tnrn, target_col, jam_col, echo_col)
        elif current_tab == 1 and self._fr is not None:
            self._freq_plot.update_plot(self._fr, target_col, jam_col, echo_col)
        elif current_tab == 4 and self._tnrn is not None and self._fs > 0:
            # Module 02 所有信号均为基带, 不需要载波去除
            self._stft_plot.update_plot(self._tnrn, target_col, jam_col, echo_col, self._fs, fc=0.0)
        elif current_tab == 5 and self._tnrn is not None:
            self._pulse_compare_plot.update_plot(self._tnrn, target_col, jam_col, echo_col)

    def clear_plots(self):
        for w in [self._time_plot, self._freq_plot,
                   self._image_plot, self._rd_plot, self._stft_plot,
                   self._pulse_compare_plot, self._trajectory_plot]:
            w.clear_data()
        self._pulse_combo.clear()
        self._mode_combo.clear()
        self._results = {}
        self._result = None
        self._current_mode = None

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
                if isinstance(widget, (ImagePlotWidget, RangeDopplerPlot, STFTPlotWidget)):
                    exporter = SVGExporter(widget._plot)
                else:
                    exporter = SVGExporter(widget._pw.plotItem)
                exporter.export(path)
            else:
                from pyqtgraph.exporters import ImageExporter
                if isinstance(widget, (ImagePlotWidget, RangeDopplerPlot, STFTPlotWidget)):
                    exporter = ImageExporter(widget._plot)
                else:
                    exporter = ImageExporter(widget._pw.plotItem)
                exporter.parameters()['width'] = 1920
                exporter.export(path)

            QMessageBox.information(self, "导出成功", f"图片已保存到:\n{path}")
        except Exception as e:
            QMessageBox.warning(self, "导出失败", str(e))

    def _export_data(self):
        if not self._result:
            QMessageBox.information(self, "提示", "请先运行干扰生成")
            return

        dir_path = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not dir_path:
            return

        mode = self._current_mode or 0
        tag = self._CASE_NAMES.get(mode, f"Case{mode}").replace(" ", "_")
        saved = []
        try:
            for key, label in [("echo_target", "合成回波"), ("jam_signal", "干扰信号"),
                                ("target_signal", "目标回波")]:
                mat = self._result.get(key)
                if mat is not None:
                    path = os.path.join(dir_path, f"02_jamming_{tag}_{label}_signal.dat")
                    self._save_complex_matrix(mat, path)
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
