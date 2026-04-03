import os
import sys
import numpy as np
import warnings
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTabWidget,
    QLabel, QPushButton, QComboBox, QFileDialog,
    QMessageBox, QButtonGroup,
)
from PySide6.QtCore import Qt, Signal, QTimer

import pyqtgraph as pg
from scipy.fft import fft, fftshift
from scipy.signal import stft as scipy_stft

from ui.theme import PLOT_THEMES, WIDGET_THEMES

# ── Module-level theme state ──
_current_plot_theme = "light"

# Windows 启用 OpenGL 加速渲染; macOS 已有 Metal 加速无需额外开启
_USE_OPENGL = sys.platform == "win32"
_AA = True  # OpenGL 模式下 GPU 处理抗锯齿无额外开销


def apply_plot_theme(theme="light"):
    """Switch pyqtgraph global colors and update module state."""
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


# Apply default light theme on import
apply_plot_theme("light")


def _pt():
    """Shortcut to current plot theme colors."""
    return PLOT_THEMES.get(_current_plot_theme, PLOT_THEMES["light"])


_AXIS_FONT = pg.QtGui.QFont("PingFang SC", 11)
_TICK_FONT = pg.QtGui.QFont("Menlo", 10)

# Default label style — every setLabel() call must include color,
# otherwise pyqtgraph defaults to #969696 gray which is invisible.
_LABEL_STYLE = {'color': '#5a6a7a', 'font-size': '11pt', 'font-family': 'PingFang SC'}


def _set_label(plot_item_or_axis, axis, text, units=None):
    """Wrapper around setLabel that always injects theme-correct label color."""
    style = dict(_LABEL_STYLE)
    pt = _pt()
    style['color'] = pt["text"]
    kwargs = dict(style)
    if units:
        plot_item_or_axis.setLabel(axis, text, units=units, **kwargs)
    else:
        plot_item_or_axis.setLabel(axis, text, **kwargs)


def _apply_axis_style(plot_item):
    pt = _pt()
    _LABEL_STYLE['color'] = pt["text"]
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
        pt = _pt()
        self.getViewBox().setBackgroundColor(pt["viewbox_bg"])
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


class TimeDomainPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._pw = pg.PlotWidget(axisItems={'left': pg.AxisItem('left')})
        self._pw.setTitle("时域波形", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "快时间", units="s")
        _set_label(self._pw, "left", "幅度")

        self._c_real = self._pw.plot(pen=pg.mkPen(pt["blue"], width=1.5), name="实部",
                                      clipToView=True, downsample=2)
        self._c_imag = self._pw.plot(pen=pg.mkPen(pt["red"], width=1.5), name="虚部",
                                      clipToView=True, downsample=2)
        self._c_env = self._pw.plot(pen=pg.mkPen(pt["green"], width=1.5), name="包络",
                                     clipToView=True, downsample=2)

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

        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)
        self._btn_group = QButtonGroup(self)
        self._btn_group.setExclusive(True)

        pt = _pt()
        for idx, (name, color_key) in enumerate([
            ("实部", "blue"), ("虚部", "red"), ("包络", "green")
        ]):
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("curveToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 0)
            btn.setStyleSheet(
                f"QPushButton[curveToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[curveToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._btn_group.addButton(btn, idx)
            btn_row.addWidget(btn)

        btn_row.addStretch()
        self._btn_group.idToggled.connect(self._on_toggle)

        layout.addWidget(self._pw, stretch=1)
        layout.addLayout(btn_row)

        self._mode = 0
        self._tnrn = None
        self._col = None
        self._apply_visibility()

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

    def _on_toggle(self, btn_id, checked):
        if checked:
            self._mode = btn_id
            self._apply_visibility()
            if self._tnrn is not None and self._col is not None:
                self._pw.getViewBox().autoRange()

    def _apply_visibility(self):
        self._c_real.setVisible(self._mode == 0)
        self._c_imag.setVisible(self._mode == 1)
        self._c_env.setVisible(self._mode == 2)

    def update_plot(self, tnrn, col):
        if col is None or len(tnrn) == 0:
            return
        self._tnrn = tnrn
        self._col = col
        self._c_real.setData(tnrn, col.real)
        self._c_imag.setData(tnrn, col.imag)
        self._c_env.setData(tnrn, np.abs(col))
        self._apply_visibility()
        self._pw.getViewBox().autoRange()

    def clear_data(self):
        self._c_real.setData([], [])
        self._c_imag.setData([], [])
        self._c_env.setData([], [])
        self._tnrn = None
        self._col = None


class FrequencyPlot(_BasePlot):

    def __init__(self, parent=None):
        super().__init__("频谱 (幅度)", parent)
        pt = _pt()
        _set_label(self, "bottom", "频率", units="MHz")
        _set_label(self, "left", "幅度", units="dB")
        self._curve = self.plot(pen=pg.mkPen(pt["purple"], width=1.5),
                                 clipToView=True, downsample=2)

    def _format_crosshair(self, pos):
        x, y = pos.x(), pos.y()
        self._crosshair_label.setText(f"f={x:.2f} MHz  |H|={y:.1f} dB")
        self._crosshair_label.setPos(x, y)
        self._crosshair_label.setVisible(True)

    def update_plot(self, fr, col):
        if col is None or len(col) == 0:
            return
        spec = fftshift(fft(col))
        mag = 20.0 * np.log10(np.abs(spec) + 1e-12)
        self._curve.setData(fr / 1e6, mag)
        self.getViewBox().autoRange()


class PhasePlot(_BasePlot):

    def __init__(self, parent=None):
        super().__init__("相位谱", parent)
        pt = _pt()
        _set_label(self, "bottom", "频率", units="MHz")
        _set_label(self, "left", "相位", units="rad")
        self._curve = self.plot(pen=pg.mkPen(pt["orange"], width=1.5),
                                 clipToView=True, downsample=2)

    def _format_crosshair(self, pos):
        x, y = pos.x(), pos.y()
        self._crosshair_label.setText(f"f={x:.2f} MHz  φ={y:.2f} rad")
        self._crosshair_label.setPos(x, y)
        self._crosshair_label.setVisible(True)

    def update_plot(self, fr, col):
        if col is None or len(col) == 0:
            return
        spec = fftshift(fft(col))
        phase = np.angle(spec)
        self._curve.setData(fr / 1e6, phase)
        self.getViewBox().autoRange()


class FreqSeqPlot(_BasePlot):
    """Bar chart of carrier frequency sequence per pulse."""

    def __init__(self, parent=None):
        super().__init__("载频序列", parent)
        _set_label(self, "bottom", "脉冲索引")
        _set_label(self, "left", "频率", units="MHz")
        self._bar = None

    def update_plot(self, f_seq):
        if f_seq is None or len(f_seq) == 0:
            return
        # Bug 2 fix: remove only the bar, not all items (crosshair preserved)
        if self._bar is not None:
            self.plotItem.removeItem(self._bar)
            self._bar = None
        x = np.arange(len(f_seq))
        pt = _pt()
        self._bar = pg.BarGraphItem(x=x, height=f_seq / 1e6, width=0.8,
                              brush=pg.mkBrush(pt["teal"]), pen=pg.mkPen(pt["teal"]))
        self.plotItem.addItem(self._bar)
        self.getViewBox().autoRange()

    def clear_data(self):
        if self._bar is not None:
            self.plotItem.removeItem(self._bar)
            self._bar = None


class PhaseSeqPlot(_BasePlot):
    """Bug 5 fix: Visualize random phase sequence phi1 for Cases 2 and 5."""

    def __init__(self, parent=None):
        super().__init__("随机相位序列", parent)
        _set_label(self, "bottom", "脉冲索引")
        _set_label(self, "left", "相位", units="rad")
        self._scatter = None

    def update_plot(self, phi1):
        if phi1 is None or len(phi1) == 0:
            return
        if self._scatter is not None:
            self.plotItem.removeItem(self._scatter)
            self._scatter = None
        x = np.arange(len(phi1))
        angles = np.angle(phi1)
        pt = _pt()
        self._scatter = pg.ScatterPlotItem(
            x=x, y=angles,
            pen=pg.mkPen(None),
            brush=pg.mkBrush(pt["purple"]),
            size=8,
        )
        self.plotItem.addItem(self._scatter)
        self.getViewBox().autoRange()

    def clear_data(self):
        if self._scatter is not None:
            self.plotItem.removeItem(self._scatter)
            self._scatter = None


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

        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)
        self._mode_group = QButtonGroup(self)
        self._mode_group.setExclusive(True)

        pt = _pt()
        modes = [("实部", 0, "blue"), ("虚部", 1, "red"),
                 ("幅度", 2, "green"), ("相位", 3, "purple")]
        for name, idx, color_key in modes:
            color = pt[color_key]
            btn = QPushButton(name)
            btn.setObjectName("imgToggle")
            btn.setCheckable(True)
            btn.setChecked(idx == 3)
            btn.setStyleSheet(
                f"QPushButton[imgToggle] {{ color: {color}; border: 1.5px solid {color}; "
                f"background: transparent; border-radius: 4px; padding: 2px 12px; "
                f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
                f"QPushButton[imgToggle]:checked {{ background: {color}; color: #fff; }}"
            )
            self._mode_group.addButton(btn, idx)
            btn_row.addWidget(btn)
        btn_row.addStretch()

        self._display_mode = 3
        self._mode_group.idToggled.connect(self._on_mode_toggle)
        self._radar_sig = None

        outer.addWidget(self._layout_widget, stretch=1)
        outer.addLayout(btn_row)

    def _on_mode_toggle(self, btn_id, checked):
        if checked:
            self._display_mode = btn_id
            if self._radar_sig is not None:
                self._render()

    def _render(self):
        sig = self._radar_sig
        if sig is None:
            return

        if self._display_mode == 0:
            data = sig.real[::-1]
            title = "信号矩阵 — 实部"
            cmap = "viridis"
        elif self._display_mode == 1:
            data = sig.imag[::-1]
            title = "信号矩阵 — 虚部"
            cmap = "viridis"
        elif self._display_mode == 2:
            data = np.abs(sig)[::-1]
            title = "信号矩阵 — 幅度"
            cmap = "viridis"
        else:
            data = np.angle(sig)[::-1]
            title = "信号矩阵 — 相位 (rad)"
            cmap = "CET-D1A"

        self._img.setColorMap(pg.colormap.get(cmap))
        vmin = float(np.min(data))
        vmax = float(np.max(data))
        if vmin >= vmax:
            vmax = vmin + 1
        self._img.setImage(data, autoLevels=False)
        self._img.setLevels([vmin, vmax])
        nr, nc = sig.shape
        self._img.setRect(pg.QtCore.QRectF(0, 0, nc, nr))
        self._plot.setTitle(title)
        _set_label(self._plot, "bottom", "脉冲索引",
                            units=f"  [0~{nc - 1}]")
        _set_label(self._plot, "left", "距离单元",
                            units=f"  [0~{nr - 1}]")
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

    def update_plot(self, radar_sig):
        if radar_sig is None:
            return
        self._radar_sig = radar_sig
        self._render()

    def update_levels(self):
        sig = self._radar_sig
        if sig is None:
            return
        if self._display_mode == 0:
            data = sig.real[::-1]
        elif self._display_mode == 1:
            data = sig.imag[::-1]
        elif self._display_mode == 2:
            data = np.abs(sig)[::-1]
        else:
            data = np.angle(sig)[::-1]
        vmin = np.min(data)
        vmax = np.max(data)
        if vmin >= vmax:
            vmax = vmin + 1
        self._img.setLevels([vmin, vmax])
        self._lut.setLevels(vmin, vmax)

    def clear_data(self):
        self._img.clear()
        self._radar_sig = None


class STFTPlotWidget(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])
        self._plot = self._layout_widget.addPlot(
            title="STFT 时频分析",
            axisItems={'left': pg.AxisItem('left')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "时间", units="μs")
        _set_label(self._plot, "left", "频率", units="MHz")

        self._img = pg.ImageItem()
        # MATLAB jet colormap: 深蓝 → 青 → 绿 → 黄 → 深红
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

        self._cache_key = None

    def update_plot(self, tnrn, col, fs, fc=0.0):
        if col is None or len(col) == 0 or tnrn is None:
            return

        cache_key = (id(col), col.data.tobytes()[:64], fc)
        if self._cache_key == cache_key:
            return
        self._cache_key = cache_key

        # ── 载频去除: 使用数字化载频 fc/fs*n ──
        # 信号生成使用 exp(j*2*pi*(fc/fs)*n), 去除也用同样方式
        if fc > 0 and fs > 0:
            n = np.arange(len(col))
            carrier_phase = 2.0 * np.pi * (fc / fs) * n
            col_bb = col * np.exp(-1j * carrier_phase)
        else:
            col_bb = col

        # ── STFT parameters ──
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
        mag = np.abs(Zxx)
        mag_db = 20.0 * np.log10(mag + 1e-12)

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

    def clear_data(self):
        self._img.clear()
        self._cache_key = None


class PlotPanel(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._results = {}
        self._current_mode = None
        self._tnrn = None
        self._fr = None
        self._fc = 0.0
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

        wt = WIDGET_THEMES.get(_current_plot_theme, WIDGET_THEMES["light"])

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
        self._freq_plot = FrequencyPlot()
        self._image_plot = ImagePlotWidget()
        self._phase_plot = PhasePlot()
        self._freq_seq_plot = FreqSeqPlot()
        self._phase_seq_plot = PhaseSeqPlot()  # Bug 5 fix: phi1 visualization
        self._stft_plot = STFTPlotWidget()

        self._tabs.addTab(self._time_plot, " 时域波形 ")
        self._tabs.addTab(self._freq_plot, " 频谱 ")
        self._tabs.addTab(self._image_plot, " 信号矩阵 ")
        self._tabs.addTab(self._phase_plot, " 相位谱 ")
        self._tabs.addTab(self._freq_seq_plot, " 载频序列 ")
        self._tabs.addTab(self._phase_seq_plot, " 随机相位 ")
        self._tabs.addTab(self._stft_plot, " STFT时频 ")
        self._tabs.currentChanged.connect(self._on_tab_changed)

        layout.addWidget(self._tabs)

    def _on_tab_changed(self, idx):
        if self._current_mode and self._current_mode in self._results:
            pulse_idx = self._pulse_combo.currentIndex()
            if pulse_idx >= 0:
                self._force_update_visible(idx, pulse_idx)

    def _force_update_visible(self, tab_idx, pulse_idx):
        result = self._results.get(self._current_mode)
        if not result or result["radar_sig"] is None:
            return
        col = result["radar_sig"][:, pulse_idx]
        if tab_idx == 0 and self._tnrn is not None:
            self._time_plot.update_plot(self._tnrn, col)
        elif tab_idx == 1 and self._fr is not None:
            self._freq_plot.update_plot(self._fr, col)
        elif tab_idx == 3 and self._fr is not None:
            self._phase_plot.update_plot(self._fr, col)
        elif tab_idx == 6 and self._tnrn is not None and len(self._tnrn) > 1:
            fs = 1.0 / (self._tnrn[1] - self._tnrn[0])
            self._stft_plot.update_plot(self._tnrn, col, fs, self._fc)

    def set_time_freq_axes(self, tnrn, fr, fc=0.0):
        self._tnrn = tnrn
        self._fr = fr
        self._fc = fc

    def update_plots(self, results, default_mode=None):
        self._results = results if results else {}

        self._mode_combo.blockSignals(True)
        self._mode_combo.clear()

        mode_names = {
            1: "Case1 固定跳频",
            2: "Case2 随机相位",
            3: "Case3 PRI抖动",
            4: "Case4 混合波形",
            5: "Case5 复合波形",
        }

        mode_list = sorted(self._results.keys())
        for m in mode_list:
            self._mode_combo.addItem(mode_names.get(m, f"Case{m}"), userData=m)

        if default_mode and default_mode in self._results:
            idx = mode_list.index(default_mode)
        else:
            idx = 0
        if self._mode_combo.count() > 0:
            self._mode_combo.setCurrentIndex(idx)
        self._mode_combo.blockSignals(False)

        self._refresh_plots(mode_list[idx] if mode_list else None)

    def _on_mode_changed(self, idx):
        if idx < 0:
            return
        mode = self._mode_combo.itemData(idx)
        if mode is not None and mode in self._results:
            self._refresh_plots(mode)

    def _on_pulse_changed(self, idx):
        if idx < 0:
            return
        if self._current_mode is None or self._current_mode not in self._results:
            return
        self._pending_pulse = idx
        self._pulse_timer.start()

    def _do_update_pulse(self):
        if self._pending_pulse is not None:
            self._update_single_pulse(self._pending_pulse)
            self._pending_pulse = None

    def _refresh_plots(self, mode):
        self._current_mode = mode
        if mode is None or mode not in self._results:
            return

        result = self._results[mode]
        radar_sig = result["radar_sig"]
        f_seq = result.get("f")
        phi1 = result.get("phi1")

        if radar_sig is not None:
            nan1 = radar_sig.shape[1]

            self._pulse_combo.blockSignals(True)
            self._pulse_combo.clear()
            for i in range(nan1):
                self._pulse_combo.addItem(f"脉冲 {i}", userData=i)
            default_pulse = min(32, nan1 - 1)
            self._pulse_combo.setCurrentIndex(default_pulse)
            self._pulse_combo.blockSignals(False)

            self._image_plot.update_plot(radar_sig)
            self._update_single_pulse(default_pulse)

            if f_seq is not None and np.any(f_seq != 0):
                self._freq_seq_plot.update_plot(f_seq)

            # Bug 5 fix: visualize phi1 (random phase) for Cases 2 and 5
            if phi1 is not None and np.any(np.abs(phi1) > 0):
                self._phase_seq_plot.update_plot(phi1)

    def _update_single_pulse(self, pulse_idx):
        if self._current_mode is None or self._current_mode not in self._results:
            return
        result = self._results[self._current_mode]
        radar_sig = result["radar_sig"]
        if radar_sig is None:
            return
        if pulse_idx < 0 or pulse_idx >= radar_sig.shape[1]:
            return
        col = radar_sig[:, pulse_idx]

        current_tab = self._tabs.currentIndex()

        if self._tnrn is not None and current_tab == 0:
            self._time_plot.update_plot(self._tnrn, col)
        if self._fr is not None and current_tab in (1, 3):
            if current_tab == 1:
                self._freq_plot.update_plot(self._fr, col)
            else:
                self._phase_plot.update_plot(self._fr, col)
        if current_tab == 6 and self._tnrn is not None and len(self._tnrn) > 1:
            fs = 1.0 / (self._tnrn[1] - self._tnrn[0])
            self._stft_plot.update_plot(self._tnrn, col, fs, self._fc)

    def clear_plots(self):
        for w in [self._time_plot, self._freq_plot, self._phase_plot,
                   self._image_plot, self._freq_seq_plot, self._phase_seq_plot,
                   self._stft_plot]:
            w.clear_data()
        self._pulse_combo.clear()
        self._mode_combo.clear()
        self._results = {}
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
                if isinstance(widget, (ImagePlotWidget, STFTPlotWidget)):
                    exporter = SVGExporter(widget._plot)
                elif isinstance(widget, TimeDomainPlot):
                    exporter = SVGExporter(widget._pw.plotItem)
                else:
                    exporter = SVGExporter(widget.plotItem)
                exporter.export(path)
            else:
                from pyqtgraph.exporters import ImageExporter
                if isinstance(widget, (ImagePlotWidget, STFTPlotWidget)):
                    exporter = ImageExporter(widget._plot)
                elif isinstance(widget, TimeDomainPlot):
                    exporter = ImageExporter(widget._pw.plotItem)
                else:
                    exporter = ImageExporter(widget.plotItem)
                exporter.parameters()['width'] = 1920
                exporter.export(path)

            QMessageBox.information(self, "导出成功", f"图片已保存到:\n{path}")
        except Exception as e:
            QMessageBox.warning(self, "导出失败", str(e))

    def _export_data(self):
        if not self._results:
            QMessageBox.information(self, "提示", "请先运行波形生成")
            return

        mode_names = {
            1: "Case1", 2: "Case2", 3: "Case3", 4: "Case4", 5: "Case5",
        }
        dir_path = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not dir_path:
            return

        saved = []
        try:
            for mode, result in self._results.items():
                tag = mode_names.get(mode, f"Case{mode}")
                radar_sig = result.get("radar_sig")
                if radar_sig is not None:
                    path = os.path.join(dir_path, f"01_waveform_{tag}_信号矩阵_signal.dat")
                    self._save_complex_matrix(radar_sig, path)
                    saved.append(os.path.basename(path))

                f = result.get("f")
                if f is not None and np.any(f != 0):
                    path = os.path.join(dir_path, f"01_waveform_{tag}_跳频序列_freq_hop.dat")
                    self._save_real_vector(f, path)
                    saved.append(os.path.basename(path))

                phi1 = result.get("phi1")
                if phi1 is not None and np.any(np.abs(phi1) > 0):
                    path = os.path.join(dir_path, f"01_waveform_{tag}_随机相位_phi1.dat")
                    self._save_complex_vector(phi1, path)
                    saved.append(os.path.basename(path))

                freq_seq = result.get("freq_seq")
                if freq_seq is not None and np.any(freq_seq != 0):
                    path = os.path.join(dir_path, f"01_waveform_{tag}_载频序列_freq_seq.dat")
                    self._save_real_vector(freq_seq, path)
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
            f.write(f"{n} 1\n")
            for v in vec:
                f.write(f"( {v.real:.15e} + {v.imag:.15e}j )\n")

    @staticmethod
    def _save_real_vector(vec, path):
        n = len(vec)
        with open(path, "w") as f:
            f.write(f"{n} 1\n")
            for v in vec:
                f.write(f"{v:.15e}\n")
