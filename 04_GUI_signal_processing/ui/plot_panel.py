"""
可视化面板 — 信号处理 GUI (模块04)

架构与 01/02/03 一致:
  - PlotPanel._results 字典存储所有已完成的结果
  - 工具栏: case 选择 + 共享脉冲选择 + 导出按钮
  - 切换 case/脉冲/tab 时同步刷新

标签页:
  1. 脉冲特性分析 — 每脉冲峰值频率 + 跨脉冲相位
  2. 频域频谱 — FFT 幅度 dB (单脉冲)
  3. 信号矩阵 — 2D 热力图 实部/虚部/幅度/相位
  4. 距离-多普勒图 — Cases 1-5 RD map (jet colormap)
  5. 解耦结果 — Case 6 干扰/目标/原始 (单脉冲)
  6. 结果摘要 — 峰值功率/ISR 统计柱状图
"""

import os
import sys
import warnings
import numpy as np
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTabWidget,
    QLabel, QPushButton, QFileDialog, QMessageBox,
    QButtonGroup, QComboBox,
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
    """创建信号类型切换按钮组, 返回 (layout, button_group)"""
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
            f"QPushButton#{group_name} {{ color: {color}; border: 1.5px solid {color}; "
            f"background: transparent; border-radius: 4px; padding: 2px 12px; "
            f"font-size: 11px; font-weight: bold; min-width: 50px; }}"
            f"QPushButton#{group_name}:checked {{ background: {color}; color: #fff; }}"
        )
        btn_group.addButton(btn, idx)
        btn_row.addWidget(btn)
    btn_row.addStretch()
    return btn_row, btn_group


def _add_crosshair(pw, fmt_str):
    """添加十字准线到 PlotWidget, 返回 (vline, hline, crosshair)"""
    pt = _pt()
    cross_color = pt.get("crosshair", "#999")
    vline = pg.InfiniteLine(angle=90, pen=pg.mkPen(cross_color, width=1, style=Qt.PenStyle.DashLine))
    hline = pg.InfiniteLine(angle=0, pen=pg.mkPen(cross_color, width=1, style=Qt.PenStyle.DashLine))
    vline.setVisible(False)
    hline.setVisible(False)
    pw.addItem(vline, ignoreBounds=True)
    pw.addItem(hline, ignoreBounds=True)
    crosshair = pg.TextItem(anchor=(0, 1), color=pt["crosshair_text"],
                            fill=pg.mkBrush(*pt["crosshair_fill"]))
    crosshair.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
    crosshair.setPos(0, 0)
    crosshair.setVisible(False)
    pw.addItem(crosshair, ignoreBounds=True)

    def on_mouse(evt):
        pos = pw.getViewBox().mapSceneToView(evt)
        if pw.getViewBox().sceneBoundingRect().contains(evt):
            vline.setPos(pos.x())
            hline.setPos(pos.y())
            vline.setVisible(True)
            hline.setVisible(True)
            x, y = pos.x(), pos.y()
            crosshair.setText(fmt_str.format(x=x, y=y))
            crosshair.setPos(x, y)
            crosshair.setVisible(True)
        else:
            vline.setVisible(False)
            hline.setVisible(False)
            crosshair.setVisible(False)

    pw.scene().sigMouseMoved.connect(on_mouse)
    return vline, hline, crosshair


# ═══════════════════════════════════════════════════════════════════
#  1. 脉冲特性分析 (上图: 每脉冲FFT峰值频率, 下图: 跨脉冲解卷绕相位)
# ═══════════════════════════════════════════════════════════════════

class PulseCharacteristicPlot(QWidget):
    """双图展示各 Case 脉间差异:
    上图 — 每脉冲 FFT 峰值频率 (MHz): 跳频 Case 1/4/5 呈阶梯状, 固定载频 Case 2/3 平直
    下图 — 峰值距离单元跨脉冲解卷绕相位 (rad): 多普勒线性/随机跳变/PRI 抖动
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)

        pt = _pt()

        # ── 上图: 每脉冲 FFT 峰值频率 ──
        self._pw_freq = pg.PlotWidget()
        self._pw_freq.setTitle("每脉冲峰值频率", color=pt["pg_fg"], size="11pt")
        self._pw_freq.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw_freq)
        self._pw_freq.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw_freq, "bottom", "脉冲序号")
        _set_label(self._pw_freq, "left", "峰值频率", units="MHz")

        self._curve_freq = self._pw_freq.plot(
            pen=pg.mkPen(pt["blue"], width=1.2),
            symbol='o', symbolSize=5,
            symbolBrush=pg.mkBrush(pt["blue"]),
            clipToView=True,
        )
        _add_crosshair(self._pw_freq, "pulse={x:.0f}  f={y:.2f} MHz")

        # ── 下图: 峰值距离单元跨脉冲解卷绕相位 ──
        self._pw_phase = pg.PlotWidget()
        self._pw_phase.setTitle("跨脉冲相位 (峰值距离单元)", color=pt["pg_fg"], size="11pt")
        self._pw_phase.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw_phase)
        self._pw_phase.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw_phase, "bottom", "脉冲序号")
        _set_label(self._pw_phase, "left", "解卷绕相位", units="rad")

        self._curve_phase = self._pw_phase.plot(
            pen=pg.mkPen(pt["red"], width=1.5),
            clipToView=True,
        )
        _add_crosshair(self._pw_phase, "pulse={x:.0f}  \u03c6={y:.2f} rad")

        layout.addWidget(self._pw_freq, stretch=1)
        layout.addWidget(self._pw_phase, stretch=1)

        # 导出兼容: 默认导出上图
        self._pw = self._pw_freq

    def update_plot(self, input_sig, fs, case_label=None):
        """input_sig: (nrn, nan1) complex matrix, fs: 采样频率"""
        if input_sig is None:
            return
        if input_sig.ndim == 1:
            input_sig = input_sig.reshape(-1, 1)

        nrn, nan1 = input_sig.shape

        # ── 上图: 每脉冲 FFT 峰值频率 ──
        spec_all = fftshift(fft(input_sig, axis=0), axes=0)
        mag_all = np.abs(spec_all)
        freq_axis = fftshift(np.fft.fftfreq(nrn, 1.0 / fs))

        peak_indices = np.argmax(mag_all, axis=0)  # (nan1,)
        peak_freqs = freq_axis[peak_indices] / 1e6   # MHz

        self._curve_freq.setData(np.arange(nan1), peak_freqs)
        self._pw_freq.setTitle(
            f"每脉冲峰值频率 ({case_label})",
            color=_pt()["pg_fg"], size="11pt",
        )
        self._pw_freq.getViewBox().autoRange()

        # ── 下图: 峰值距离单元跨脉冲解卷绕相位 ──
        energy = np.sum(np.abs(input_sig) ** 2, axis=1)
        peak_range = int(np.argmax(energy))
        row = input_sig[peak_range, :]
        unwrapped_phase = np.unwrap(np.angle(row))

        self._curve_phase.setData(np.arange(nan1), unwrapped_phase)
        self._pw_phase.setTitle(
            f"跨脉冲相位 — 距离单元 {peak_range} ({case_label})",
            color=_pt()["pg_fg"], size="11pt",
        )
        self._pw_phase.getViewBox().autoRange()

    def clear_data(self):
        self._curve_freq.setData([], [])
        self._curve_phase.setData([], [])
        self._pw_freq.setTitle("每脉冲峰值频率", color=_pt()["pg_fg"], size="11pt")
        self._pw_phase.setTitle("跨脉冲相位 (峰值距离单元)", color=_pt()["pg_fg"], size="11pt")


# ═══════════════════════════════════════════════════════════════════
#  2. 频域频谱 (单脉冲 FFT)
# ═══════════════════════════════════════════════════════════════════

class FrequencyPlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._pw = pg.PlotWidget()
        self._pw.setTitle("频域频谱 (幅度)", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "频率", units="MHz")
        _set_label(self._pw, "left", "幅度", units="dB")

        self._curve = self._pw.plot(pen=pg.mkPen(pt["purple"], width=1.5),
                                    clipToView=True, downsample=2)

        _add_crosshair(self._pw, "f={x:.2f} MHz  |H|={y:.1f} dB")

        layout.addWidget(self._pw, stretch=1)

        self._data = None
        self._fs = 3.0 * 40e6
        self._pulse_idx = 0

    def update_plot(self, data, fs, pulse_idx=0, case_label=None):
        """data: 1D complex vector (单脉冲)"""
        if data is None:
            return
        self._data = data
        self._fs = fs
        self._pulse_idx = pulse_idx
        self._apply_data()

    def _apply_data(self):
        if self._data is None:
            return
        col = self._data

        spec = fftshift(fft(col))
        mag = 20.0 * np.log10(np.abs(spec) + 1e-12)
        fr = fftshift(np.fft.fftfreq(len(col), 1.0 / self._fs))
        self._curve.setData(fr / 1e6, mag)

        self._pw.setTitle(
            f"频域频谱 (脉冲 {self._pulse_idx})",
            color=_pt()["pg_fg"], size="11pt",
        )
        self._pw.getViewBox().autoRange()

    def clear_data(self):
        self._curve.setData([], [])
        self._data = None


# ═══════════════════════════════════════════════════════════════════
#  3. 信号矩阵 (2D 热力图, 与 01/02 一致)
# ═══════════════════════════════════════════════════════════════════

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

        # 显示模式切换按钮
        btn_row = QHBoxLayout()
        btn_row.setSpacing(4)
        self._mode_group = QButtonGroup(self)
        self._mode_group.setExclusive(True)

        modes = [("实部", 0, "blue"), ("虚部", 1, "red"),
                 ("幅度", 2, "green"), ("相位", 3, "purple")]
        for name, idx, color_key in modes:
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

        self._display_mode = 2  # 默认幅度
        self._mode_group.idToggled.connect(self._on_mode_toggle)
        self._radar_sig = None
        self._sig_shape = None

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

        cmap_obj = pg.colormap.get(cmap)
        self._img.setColorMap(cmap_obj)
        self._lut.gradient.setColorMap(cmap_obj)

        vmin = float(np.min(data))
        vmax = float(np.max(data))
        if vmin >= vmax:
            vmax = vmin + 1
        self._img.setImage(data, autoLevels=False)
        self._img.setLevels([vmin, vmax])

        nr, nc = sig.shape
        self._img.setRect(pg.QtCore.QRectF(0, 0, nc, nr))
        self._plot.setTitle(title)
        _set_label(self._plot, "bottom", "脉冲索引", units=f"  [0~{nc - 1}]")
        _set_label(self._plot, "left", "距离单元", units=f"  [0~{nr - 1}]")

        if self._sig_shape is None or self._sig_shape != sig.shape:
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
        """radar_sig: (nrn, nan1) complex matrix, 兼容 1D"""
        if radar_sig is None:
            return
        if radar_sig.ndim == 1:
            radar_sig = radar_sig.reshape(-1, 1)
        self._radar_sig = radar_sig
        self._render()

    def clear_data(self):
        self._img.clear()
        self._radar_sig = None
        self._sig_shape = None
        self._plot.setTitle("信号矩阵热力图", color=_pt()["pg_fg"], size="11pt")


# ═══════════════════════════════════════════════════════════════════
#  4. 距离-多普勒图 (Cases 1-5, jet colormap)
# ═══════════════════════════════════════════════════════════════════

class RDMapPlotWidget(QWidget):

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
            axisItems={'left': pg.AxisItem('left'), 'bottom': pg.AxisItem('bottom')},
        )
        self._plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._plot)
        self._plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._plot, "bottom", "速度", units="m/s")
        _set_label(self._plot, "left", "距离", units="m")

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

        # 十字准线
        cross_color = pt.get("crosshair", "#fff")
        self._vline = pg.InfiniteLine(angle=90, pen=pg.mkPen(cross_color, width=1, style=Qt.PenStyle.DashLine))
        self._hline = pg.InfiniteLine(angle=0, pen=pg.mkPen(cross_color, width=1, style=Qt.PenStyle.DashLine))
        self._vline.setVisible(False)
        self._hline.setVisible(False)
        self._plot.addItem(self._vline, ignoreBounds=True)
        self._plot.addItem(self._hline, ignoreBounds=True)
        self._crosshair = pg.TextItem(anchor=(0, 1), color=pt["crosshair_text"],
                                      fill=pg.mkBrush(*pt["crosshair_fill"]))
        self._crosshair.setFont(pg.QtGui.QFont("Menlo", 9, pg.QtGui.QFont.Weight.Bold))
        self._crosshair.setVisible(False)
        self._plot.addItem(self._crosshair, ignoreBounds=True)

        self._rd_data = None
        self._xi = None
        self._dv = None

        def on_mouse(evt):
            if self._rd_data is None:
                return
            pos = self._plot.getViewBox().mapSceneToView(evt)
            if self._plot.getViewBox().sceneBoundingRect().contains(evt):
                self._vline.setPos(pos.x())
                self._hline.setPos(pos.y())
                self._vline.setVisible(True)
                self._hline.setVisible(True)
                ix = np.argmin(np.abs(self._dv - pos.x()))
                iy = np.argmin(np.abs(self._xi - pos.y()))
                power = 20 * np.log10(np.abs(self._rd_data[iy, ix]) + 1e-15)
                # 图像已上下翻转, power 对应位置正确
                self._crosshair.setText(f"v={pos.x:.1f} m/s  r={pos.y:.1f} m  {power:.1f} dB")
                self._crosshair.setPos(pos.x(), pos.y())
                self._crosshair.setVisible(True)
            else:
                self._vline.setVisible(False)
                self._hline.setVisible(False)
                self._crosshair.setVisible(False)

        self._plot.scene().sigMouseMoved.connect(on_mouse)

        layout.addWidget(self._layout_widget, stretch=1)

    def update_plot(self, rd_map, xi, dv, case_num):
        """rd_map: (nrn x nan1) complex, xi: (nrn,) distance axis, dv: (nan1,) velocity axis"""
        if rd_map is None or xi is None or dv is None:
            return

        self._rd_data = rd_map
        self._xi = xi
        self._dv = dv

        mag_db = 20.0 * np.log10(np.abs(rd_map) + 1e-15)
        peak = float(np.max(mag_db))
        vmin = peak - 60.0
        vmax = peak

        self._img.setImage(mag_db[::-1], autoLevels=False)
        self._img.setLevels([vmin, vmax])
        self._lut.setLevels(vmin, vmax)

        nrn, nan1 = rd_map.shape
        x_min, x_max = float(dv[0]), float(dv[-1])
        y_min, y_max = float(xi[0]), float(xi[-1])
        x_range = x_max - x_min if x_max != x_min else 1.0
        y_range = y_max - y_min if y_max != y_min else 1.0
        self._img.setRect(pg.QtCore.QRectF(x_min, y_min, x_range, y_range))

        self._plot.setTitle(
            f"距离-多普勒图 — Case {case_num} ({nrn}x{nan1}, 峰值={peak:.1f} dB)",
            color=_pt()["pg_fg"], size="11pt",
        )
        self._plot.getViewBox().setLimits(
            xMin=x_min, xMax=x_max,
            yMin=y_min, yMax=y_max,
        )
        self._plot.getViewBox().setRange(
            xRange=(x_min, x_max),
            yRange=(y_min, y_max),
            padding=0.02,
        )

    def clear_data(self):
        self._img.clear()
        self._rd_data = None
        self._xi = None
        self._dv = None


# ═══════════════════════════════════════════════════════════════════
#  5. 解耦结果 (Case 6, 单脉冲, 无内部脉冲选择器)
# ═══════════════════════════════════════════════════════════════════

class DecouplePlot(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(4)
        self._sig_group = QButtonGroup(self)
        self._sig_group.setExclusive(True)
        for idx, (name, color_key) in enumerate([
            ("原始混合", "blue"), ("分离干扰", "red"),
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

        self._pw = pg.PlotWidget()
        self._pw.setTitle("解耦结果", color=pt["pg_fg"], size="11pt")
        self._pw.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._pw)
        self._pw.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._pw, "bottom", "快时间", units="us")
        _set_label(self._pw, "left", "包络幅度")

        self._c_mix = self._pw.plot(pen=pg.mkPen(pt["blue"], width=1.5),
                                    name="原始混合", clipToView=True, downsample=2)
        self._c_jam = self._pw.plot(pen=pg.mkPen(pt["red"], width=1.5),
                                    name="分离干扰", clipToView=True, downsample=2)
        self._c_tgt = self._pw.plot(pen=pg.mkPen(pt["green"], width=1.5),
                                    name="分离目标", clipToView=True, downsample=2)

        lg = self._pw.addLegend(offset=(10, 10), labelTextColor=pt["crosshair_text"],
                                brush=pg.mkBrush(*pt["crosshair_fill"]),
                                pen=pg.mkPen(pt["axis"]))
        lg.addItem(self._c_mix, "原始混合")
        lg.addItem(self._c_jam, "分离干扰")
        lg.addItem(self._c_tgt, "分离目标")

        _add_crosshair(self._pw, "t={x:.3f} us  |x|={y:.4f}")

        layout.addWidget(self._pw, stretch=1)

        self._sig_type = 3
        self._input_sig = None
        self._jam_sig = None
        self._tgt_sig = None
        self._fs = 3.0 * 40e6
        self._pulse_idx = 0

        self._sig_group.idToggled.connect(self._on_sig_toggle)

    def _on_sig_toggle(self, btn_id, checked):
        if checked:
            self._sig_type = btn_id
            self._apply_visibility()

    def _apply_data(self):
        if self._input_sig is None:
            return

        col_mix = self._input_sig
        col_jam = self._jam_sig
        col_tgt = self._tgt_sig
        t_axis = np.arange(len(col_mix)) / self._fs

        t_us = t_axis * 1e6
        self._c_mix.setData(t_us, np.abs(col_mix))
        self._c_jam.setData((t_us, np.abs(col_jam)) if col_jam is not None else ([], []))
        self._c_tgt.setData((t_us, np.abs(col_tgt)) if col_tgt is not None else ([], []))
        self._apply_visibility()

    def _apply_visibility(self):
        if self._sig_type == 3:
            self._c_mix.setVisible(True)
            self._c_jam.setVisible(True)
            self._c_tgt.setVisible(True)
        else:
            self._c_mix.setVisible(self._sig_type == 0)
            self._c_jam.setVisible(self._sig_type == 1)
            self._c_tgt.setVisible(self._sig_type == 2)
        self._pw.getViewBox().autoRange()

    def update_plot(self, input_sig, jam_sig, tgt_sig, fs, pulse_idx=0, jam_type=None):
        """input_sig, jam_sig, tgt_sig: 1D complex vectors (单脉冲)"""
        self._input_sig = input_sig
        self._jam_sig = jam_sig
        self._tgt_sig = tgt_sig
        self._fs = fs
        self._pulse_idx = pulse_idx

        if jam_type is not None:
            jam_names = {1: "ISDJ", 2: "ISRJ", 3: "ISCJ", 4: "NBJ", 5: "RDJ"}
            jam_name = jam_names.get(jam_type, f"Type {jam_type}")
            self._pw.setTitle(
                f"Case 6 解耦结果 — {jam_name} (脉冲 {pulse_idx})",
                color=_pt()["pg_fg"], size="11pt",
            )

        self._apply_data()

    def clear_data(self):
        self._c_mix.setData([], [])
        self._c_jam.setData([], [])
        self._c_tgt.setData([], [])
        self._input_sig = None
        self._jam_sig = None
        self._tgt_sig = None


# ═══════════════════════════════════════════════════════════════════
#  6. 结果摘要 (RD 峰值对比 + 解耦统计)
# ═══════════════════════════════════════════════════════════════════

class ResultSummaryPlot(QWidget):

    _CASE_COLORS = {
        1: "#3478f6", 2: "#1a8a7d", 3: "#2ea44f",
        4: "#d47216", 5: "#8250df",
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        pt = _pt()

        self._layout_widget = pg.GraphicsLayoutWidget()
        self._layout_widget.setBackground(pt["pg_bg"])

        self._bar_plot = self._layout_widget.addPlot(
            title="结果摘要",
            axisItems={'left': pg.AxisItem('left'), 'bottom': pg.AxisItem('bottom')},
        )
        self._bar_plot.showGrid(x=True, y=True, alpha=0.4)
        _apply_axis_style(self._bar_plot)
        self._bar_plot.getViewBox().setBackgroundColor(pt["viewbox_bg"])
        _set_label(self._bar_plot, "bottom", "类别")
        _set_label(self._bar_plot, "left", "数值")
        self._plot = self._bar_plot  # 导出兼容

        self._bars = []
        self._case_bars = {}    # case_num → BarGraphItem
        self._case_peaks = {}   # case_num → peak dB
        self._bar_width = 0.6
        self._mode = None       # "rd" / "decouple" / None

        self._info_text = pg.TextItem(anchor=(0, 0), color=pt["text"])
        self._info_text.setFont(pg.QtGui.QFont("Menlo", 10))
        self._info_text.setPos(0, 0)
        self._bar_plot.addItem(self._info_text, ignoreBounds=True)

        layout.addWidget(self._layout_widget, stretch=1)

    def _clear_bars(self):
        for bar in self._bars:
            self._bar_plot.removeItem(bar)
        self._bars = []
        self._case_bars = {}
        self._case_peaks = {}

    def update_all_rd(self, rd_results):
        """rd_results: {case_num: result_dict, ...} — 重建所有 RD 柱状图"""
        self._clear_bars()
        self._mode = "rd"

        for case_num, r in rd_results.items():
            rd_map = r.get("rd_map")
            peak = float(20 * np.log10(np.max(np.abs(rd_map)) + 1e-15)) if rd_map is not None else 0

            color = self._CASE_COLORS.get(case_num, "#5b8def")
            bar = pg.BarGraphItem(
                x=[case_num], height=[peak], width=self._bar_width,
                brush=pg.mkBrush(color), pen=pg.mkPen(color, width=1),
            )
            self._bar_plot.addItem(bar)
            self._bars.append(bar)
            self._case_bars[case_num] = bar
            self._case_peaks[case_num] = peak

        if not self._case_bars:
            self._bar_plot.setTitle("结果摘要", color=_pt()["pg_fg"], size="11pt")
            self._info_text.setText("")
            return

        all_cases = sorted(self._case_bars.keys())
        n = len(all_cases)
        x_min = min(all_cases) - 0.5
        x_max = max(all_cases) + 0.5
        self._bar_plot.setXRange(x_min, x_max, padding=0.1)

        ax = self._bar_plot.getAxis('bottom')
        ax.setTicks([[(c, f"Case {c}") for c in all_cases]])

        peaks = list(self._case_peaks.values())
        y_min = min(min(peaks) - 10, -80)
        y_max = max(peaks) * 1.2 if max(peaks) > 0 else 10

        self._bar_plot.setTitle(
            f"Cases 1-5: 峰值功率对比 ({n} cases)",
            color=_pt()["pg_fg"], size="11pt",
        )

        info_lines = [f"Case {c}: {self._case_peaks[c]:.1f} dB" for c in all_cases]
        self._info_text.setText("\n".join(info_lines))
        self._info_text.setPos(x_max + 0.1, y_max * 0.85)

        self._bar_plot.setYRange(y_min, y_max, padding=0.05)

    def update_decouple(self, jam_type, isr_dB, avg_threshold, gaojiepu_count, nan1, decouple_flag, elapsed):
        self._clear_bars()
        self._mode = "decouple"

        n_bins = 3
        self._bar_plot.setXRange(-0.5, n_bins - 0.5, padding=0.1)

        if decouple_flag is not None:
            normal_count = int(np.sum(decouple_flag == 0))
            gaojiepu_count_actual = int(np.sum(decouple_flag == 1))
        else:
            normal_count = nan1 - gaojiepu_count
            gaojiepu_count_actual = gaojiepu_count

        colors = ["#5b8def", "#e07050", "#50c050"]
        labels = ["正常脉冲", "高阶谱脉冲", "ISR (dB)"]
        values = [normal_count, gaojiepu_count_actual, max(isr_dB, 0)]
        for i in range(n_bins):
            bar = pg.BarGraphItem(x=[i], height=[values[i]], width=self._bar_width,
                                  brush=pg.mkBrush(colors[i]),
                                  pen=pg.mkPen(colors[i], width=1))
            self._bar_plot.addItem(bar)
            self._bars.append(bar)

        self._bar_plot.setYRange(0, max(max(values) * 1.2, 1), padding=0.05)
        ax = self._bar_plot.getAxis('bottom')
        ax.setTicks([[(i, labels[i]) for i in range(n_bins)]])

        jam_names = {1: "ISDJ", 2: "ISRJ", 3: "ISCJ", 4: "NBJ", 5: "RDJ"}
        jam_name = jam_names.get(jam_type, f"Type {jam_type}")

        self._bar_plot.setTitle(
            f"Case 6 解耦: ISR={isr_dB:.1f} dB, 高阶谱={gaojiepu_count_actual}/{nan1}",
            color=_pt()["pg_fg"], size="11pt",
        )

        info = (
            f"Case 6 时频干扰解耦\n"
            f"干扰类型: {jam_type} ({jam_name})\n"
            f"ISR: {isr_dB:.1f} dB\n"
            f"平均阈值: {avg_threshold:.2f}\n"
            f"正常/高阶谱: {normal_count}/{gaojiepu_count_actual}\n"
            f"耗时: {elapsed*1000:.1f} ms"
        )
        self._info_text.setText(info)
        self._info_text.setPos(n_bins - 0.5, max(max(values) * 1.1, 1))

    def clear_data(self):
        self._clear_bars()
        self._info_text.setText("")
        self._bar_plot.setTitle("结果摘要", color=_pt()["pg_fg"], size="11pt")


# ═══════════════════════════════════════════════════════════════════
#  7. STFT 时频分析 (单脉冲)
# ═══════════════════════════════════════════════════════════════════

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

    def update_plot(self, col, fs, fc=0.0, pulse_idx=0, case_label=""):
        if col is None or len(col) == 0:
            return

        cache_key = (id(col), col.data.tobytes()[:64], fc)
        if self._cache_key == cache_key:
            return
        self._cache_key = cache_key

        # 载频去除 (passband 信号需要, baseband 不需要)
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

        title = f"STFT 时频分析 — {case_label} 脉冲 {pulse_idx}" if case_label else f"STFT 时频分析 — 脉冲 {pulse_idx}"
        self._plot.setTitle(title, color=_pt()["pg_fg"], size="11pt")
        self._plot.getViewBox().autoRange()

    def clear_data(self):
        self._img.clear()
        self._cache_key = None


# ═══════════════════════════════════════════════════════════════════
#  PlotPanel 主面板 (架构与 01/02/03 一致)
# ═══════════════════════════════════════════════════════════════════

# Tab 索引
_TAB_TIME = 0
_TAB_FREQ = 1
_TAB_IMAGE = 2
_TAB_RD = 3
_TAB_DECOUPLE = 4
_TAB_SUMMARY = 5
_TAB_STFT = 6

# Case 显示名
_CASE_NAMES = {
    1: "Case1 跳频信号处理",
    2: "Case2 固定载频处理",
    3: "Case3 传统脉冲压缩",
    4: "Case4 改进型脉冲压缩",
    5: "Case5 复合处理",
}

_JAMMING_NAMES = {
    1: "ISDJ 间歇采样直接转发",
    2: "ISRJ 间歇采样重复转发",
    3: "ISCJ 间歇采样循环转发",
    4: "NBJ 窄带瞄频噪声",
    5: "RDJ 距离欺骗干扰",
}


class PlotPanel(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self._results = {}        # {(func_type, arg): result_dict}
        self._current_key = None  # (func_type, arg)
        self._fs = 3.0 * 40e6
        self._fc = 16e9
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

        # ── 工具栏 ──
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        lbl_case = QLabel("模式:")
        lbl_case.setStyleSheet(f"color: {_pt()['text']}; font-size: 12px; font-weight: bold;")
        toolbar.addWidget(lbl_case)

        self._case_combo = QComboBox()
        self._case_combo.setMinimumWidth(200)
        self._case_combo.currentIndexChanged.connect(self._on_case_changed)
        toolbar.addWidget(self._case_combo)

        toolbar.addStretch()

        lbl_pulse = QLabel("脉冲:")
        lbl_pulse.setStyleSheet(f"color: {_pt()['text']}; font-size: 12px; font-weight: bold;")
        toolbar.addWidget(lbl_pulse)

        self._pulse_combo = QComboBox()
        self._pulse_combo.setMinimumWidth(80)
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

        # ── 标签页 ──
        self._tabs = QTabWidget()

        self._pulse_char = PulseCharacteristicPlot()
        self._freq_plot = FrequencyPlot()
        self._image_plot = ImagePlotWidget()
        self._rd_plot = RDMapPlotWidget()
        self._decouple_plot = DecouplePlot()
        self._summary_plot = ResultSummaryPlot()
        self._stft_plot = STFTPlotWidget()

        self._tabs.addTab(self._pulse_char, " 脉冲特性分析 ")
        self._tabs.addTab(self._freq_plot, " 频域频谱 ")
        self._tabs.addTab(self._image_plot, " 信号矩阵 ")
        self._tabs.addTab(self._rd_plot, " 距离-多普勒 ")
        self._tabs.addTab(self._decouple_plot, " 解耦结果 ")
        self._tabs.addTab(self._summary_plot, " 结果摘要 ")
        self._tabs.addTab(self._stft_plot, " STFT时频 ")

        self._tabs.currentChanged.connect(self._on_tab_changed)

        layout.addWidget(self._tabs)

    # ── 公共接口 (与 main_window 对接) ──

    def update_result(self, func_type, arg, result):
        """存储结果, 重建 case combo, 切换到新 case, 刷新绘图"""
        key = (func_type, arg)
        self._results[key] = result
        self._fs = result.get("_fs", 3.0 * 40e6)
        self._fc = result.get("_fc", 16e9)

        # 重建 case combo 并选中新 case
        self._rebuild_case_combo(key)

        # 刷新所有绘图
        self._refresh_plots(key)

        # 自动切换到最相关标签
        if func_type == "processing_rd":
            self._tabs.setCurrentIndex(_TAB_RD)
        elif func_type == "processing_decouple":
            self._tabs.setCurrentIndex(_TAB_DECOUPLE)

    def refresh_current(self):
        """主题切换时重绘"""
        if self._current_key is not None and self._current_key in self._results:
            self._refresh_plots(self._current_key)

    def clear_plots(self):
        self._pulse_timer.stop()
        self._pending_pulse = None
        for w in [self._pulse_char, self._freq_plot, self._image_plot,
                   self._rd_plot, self._decouple_plot, self._summary_plot]:
            w.clear_data()
        self._pulse_combo.clear()
        self._case_combo.clear()
        self._results = {}
        self._current_key = None

    # ── Case 选择 ──

    def _rebuild_case_combo(self, select_key=None):
        self._case_combo.blockSignals(True)
        self._case_combo.clear()

        # RD cases 排前面, decouple cases 排后面
        sorted_keys = sorted(self._results.keys(), key=lambda k: (
            0 if k[0] == "processing_rd" else 1, k[1]
        ))

        select_idx = 0
        for i, (ft, a) in enumerate(sorted_keys):
            if ft == "processing_rd":
                label = _CASE_NAMES.get(a, f"Case {a}")
            else:
                jam_name = _JAMMING_NAMES.get(a, f"Type {a}")
                label = f"Case6 解耦 — {jam_name}"
            self._case_combo.addItem(label, userData=(ft, a))
            if (ft, a) == select_key:
                select_idx = i

        if self._case_combo.count() > 0:
            self._case_combo.setCurrentIndex(select_idx)
        self._case_combo.blockSignals(False)

    def _on_case_changed(self, idx):
        if idx < 0:
            return
        key = self._case_combo.itemData(idx)
        if key is not None and key in self._results:
            self._refresh_plots(key)

    # ── 脉冲选择 (用于 Tab 1/4, 30ms 防抖) ──

    def _on_pulse_changed(self, idx):
        if idx < 0 or self._current_key is None:
            return
        self._pending_pulse = idx
        self._pulse_timer.start()

    def _do_update_pulse(self):
        if self._pending_pulse is not None:
            self._update_single_pulse(self._pending_pulse)
            self._pending_pulse = None

    # ── 标签页切换 ──

    def _on_tab_changed(self, idx):
        if self._current_key is None or self._current_key not in self._results:
            return
        if idx == _TAB_TIME:
            self._update_slow_time()
        else:
            pulse_idx = self._pulse_combo.currentIndex()
            if pulse_idx >= 0:
                self._force_update_visible(idx, pulse_idx)

    def _force_update_visible(self, tab_idx, pulse_idx):
        """切换标签页时强制刷新频域/解耦图表"""
        result = self._results.get(self._current_key)
        if not result:
            return
        func_type = self._current_key[0]

        input_sig = result.get("input_signal")
        if input_sig is None:
            return

        if input_sig.ndim == 1:
            col = input_sig
        elif pulse_idx < input_sig.shape[1]:
            col = input_sig[:, pulse_idx]
        else:
            return

        case_label = self._get_case_label()

        if tab_idx == _TAB_FREQ:
            self._freq_plot.update_plot(col, self._fs, pulse_idx, case_label)
        elif tab_idx == _TAB_DECOUPLE and func_type == "processing_decouple":
            arg = self._current_key[1]
            self._update_decouple_pulse(result, arg, pulse_idx)
        elif tab_idx == _TAB_STFT:
            # Cases 1/2/5 为 passband (需去载波), Cases 3/4/6 为 baseband (不需)
            stft_fc = self._fc if func_type == "processing_rd" and arg in (1, 2, 5) else 0.0
            self._stft_plot.update_plot(col, self._fs, stft_fc, pulse_idx, case_label)

    # ── 核心: 刷新所有绘图 ──

    def _refresh_plots(self, key):
        self._pulse_timer.stop()
        self._pending_pulse = None
        self._current_key = key
        if key is None or key not in self._results:
            return

        result = self._results[key]
        func_type, arg = key
        self._fs = result.get("_fs", 3.0 * 40e6)

        input_sig = result.get("input_signal")

        # 计算脉冲数
        if input_sig is not None:
            max_pulse = input_sig.shape[1] - 1 if input_sig.ndim > 1 else 0
        else:
            max_pulse = 0

        # 重建脉冲 combo
        self._pulse_combo.blockSignals(True)
        self._pulse_combo.clear()
        for i in range(max_pulse + 1):
            self._pulse_combo.addItem(f"脉冲 {i}", userData=i)
        default_pulse = min(32, max_pulse)
        self._pulse_combo.setCurrentIndex(default_pulse)
        self._pulse_combo.blockSignals(False)

        # ── 更新信号矩阵 (全矩阵) ──
        if input_sig is not None:
            self._image_plot.update_plot(input_sig)
        else:
            self._pulse_char.clear_data()
            self._freq_plot.clear_data()
            self._image_plot.clear_data()

        # ── 清除不适用的图表 ──
        if func_type != "processing_rd":
            self._rd_plot.clear_data()
        if func_type != "processing_decouple":
            self._decouple_plot.clear_data()

        # ── 更新距离-多普勒图 (仅 RD) ──
        if func_type == "processing_rd":
            self._rd_plot.update_plot(
                result.get("rd_map"), result.get("xi"), result.get("dv"), arg
            )

        # ── 更新结果摘要 ──
        elapsed = result.get("elapsed", 0.0)
        if func_type == "processing_rd":
            rd_results = {}
            for (ft, a), r in self._results.items():
                if ft == "processing_rd":
                    rd_results[a] = r
            self._summary_plot.update_all_rd(rd_results)
        elif func_type == "processing_decouple":
            self._summary_plot.update_decouple(
                arg, result.get("isr_dB", 0), result.get("avg_threshold", 0),
                result.get("gaojiepu_count", 0), result.get("nan1", 0),
                result.get("decouple_flag"), elapsed,
            )

        # ── 更新单脉冲图 (频域/解耦) ──
        self._update_single_pulse(default_pulse)

        # ── 更新慢时间波形 (Tab 0, 自动选峰值距离单元) ──
        self._update_slow_time()

    def _update_slow_time(self):
        """更新脉冲特性分析图 (Tab 0)"""
        if self._current_key is None or self._current_key not in self._results:
            return
        result = self._results[self._current_key]
        input_sig = result.get("input_signal")
        if input_sig is None or input_sig.ndim < 2:
            return
        self._pulse_char.update_plot(input_sig, self._fs, self._get_case_label())

    def _update_single_pulse(self, pulse_idx):
        if self._current_key is None or self._current_key not in self._results:
            return
        result = self._results[self._current_key]
        func_type, arg = self._current_key

        input_sig = result.get("input_signal")
        if input_sig is None:
            return

        if input_sig.ndim == 1:
            col = input_sig
        elif pulse_idx < input_sig.shape[1]:
            col = input_sig[:, pulse_idx]
        else:
            return

        case_label = self._get_case_label()
        current_tab = self._tabs.currentIndex()

        if current_tab == _TAB_FREQ:
            self._freq_plot.update_plot(col, self._fs, pulse_idx, case_label)
        elif current_tab == _TAB_DECOUPLE and func_type == "processing_decouple":
            self._update_decouple_pulse(result, arg, pulse_idx)
        elif current_tab == _TAB_STFT:
            stft_fc = self._fc if func_type == "processing_rd" and arg in (1, 2, 5) else 0.0
            self._stft_plot.update_plot(col, self._fs, stft_fc, pulse_idx, case_label)

    def _update_decouple_pulse(self, result, arg, pulse_idx):
        """更新解耦图的单脉冲数据"""
        input_sig = result.get("input_signal")
        jam_sig = result.get("jam_signal")
        tgt_sig = result.get("target_signal")
        if jam_sig is None or tgt_sig is None or input_sig is None:
            return

        if input_sig.ndim > 1 and pulse_idx < input_sig.shape[1]:
            input_col = input_sig[:, pulse_idx]
        elif input_sig.ndim == 1:
            input_col = input_sig
        else:
            return

        if jam_sig.ndim > 1 and pulse_idx < jam_sig.shape[1]:
            jam_col = jam_sig[:, pulse_idx]
            tgt_col = tgt_sig[:, pulse_idx]
        elif jam_sig.ndim == 1:
            jam_col = jam_sig
            tgt_col = tgt_sig
        else:
            return

        self._decouple_plot.update_plot(input_col, jam_col, tgt_col, self._fs, pulse_idx, jam_type=arg)

    def _get_case_label(self):
        if self._current_key is None:
            return ""
        func_type, arg = self._current_key
        if func_type == "processing_rd":
            return f"Case {arg}"
        else:
            return "Case 6"

    # ── 导出 ──

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
                if isinstance(widget, (ImagePlotWidget, RDMapPlotWidget, ResultSummaryPlot, STFTPlotWidget)):
                    exporter = SVGExporter(widget._plot)
                else:
                    exporter = SVGExporter(widget._pw.plotItem)
                exporter.export(path)
            else:
                from pyqtgraph.exporters import ImageExporter
                if isinstance(widget, (ImagePlotWidget, RDMapPlotWidget, ResultSummaryPlot, STFTPlotWidget)):
                    exporter = ImageExporter(widget._plot)
                else:
                    exporter = ImageExporter(widget._pw.plotItem)
                exporter.parameters()['width'] = 1920
                exporter.export(path)

            QMessageBox.information(self, "导出成功", f"图片已保存到:\n{path}")
        except Exception as e:
            QMessageBox.warning(self, "导出失败", str(e))

    def _export_data(self):
        if not self._results:
            QMessageBox.information(self, "提示", "请先运行计算")
            return

        dir_path = QFileDialog.getExistingDirectory(self, "选择导出目录")
        if not dir_path:
            return

        saved = []
        try:
            for (func_type, arg), r in self._results.items():
                if func_type == "processing_rd":
                    tag = f"Case{arg}_RD"
                else:
                    jam_name = _JAMMING_NAMES.get(arg, f"Type{arg}")
                    tag = f"Case6_{jam_name}"

                def save_matrix(mat, name):
                    if mat is not None:
                        p = os.path.join(dir_path, f"04_{tag}_{name}.dat")
                        if mat.ndim == 1:
                            mat = mat.reshape(-1, 1)
                        nr, nc = mat.shape
                        with open(p, "w", encoding="utf-8") as f:
                            f.write(f"{nr} {nc}\n")
                            for i in range(nr):
                                for j in range(nc):
                                    v = mat[i, j]
                                    f.write(f"( {v.real:.15e} + {v.imag:.15e}j )\n")
                        saved.append(os.path.basename(p))

                def save_vector(vec, name):
                    if vec is not None:
                        p = os.path.join(dir_path, f"04_{tag}_{name}.dat")
                        n = len(vec)
                        is_complex = np.iscomplexobj(vec)
                        with open(p, "w", encoding="utf-8") as f:
                            f.write(f"{n} 1\n")
                            for i in range(n):
                                v = vec[i]
                                if is_complex:
                                    f.write(f"( {v.real:.15e} + {v.imag:.15e}j )\n")
                                else:
                                    f.write(f"{float(v):.15e}\n")
                        saved.append(os.path.basename(p))

                save_matrix(r.get("rd_map"), "RD图")
                save_matrix(r.get("input_signal"), "输入信号")
                save_matrix(r.get("jam_signal"), "分离干扰")
                save_matrix(r.get("target_signal"), "分离目标")

                flag = r.get("decouple_flag")
                if flag is not None:
                    p = os.path.join(dir_path, f"04_{tag}_解耦标志.txt")
                    np.savetxt(p, flag, fmt="%d", header="decouple_flag")
                    saved.append(os.path.basename(p))

                xi = r.get("xi")
                dv = r.get("dv")
                if xi is not None:
                    p = os.path.join(dir_path, f"04_{tag}_距离轴.txt")
                    np.savetxt(p, xi, fmt="%.15e", header="xi (m)")
                    saved.append(os.path.basename(p))
                if dv is not None:
                    p = os.path.join(dir_path, f"04_{tag}_速度轴.txt")
                    np.savetxt(p, dv, fmt="%.15e", header="dv (m/s)")
                    saved.append(os.path.basename(p))

            msg = "\n".join(saved)
            QMessageBox.information(self, "导出成功", f"已导出 {len(saved)} 个文件:\n{msg}")
        except Exception as e:
            QMessageBox.warning(self, "导出失败", str(e))
