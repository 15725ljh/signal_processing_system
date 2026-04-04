"""
信号处理工具函数 (纯 Python 实现)

提供 FFT/IFFT、滤波、窗函数等工具, 兼容 Cython 版本接口
"""

import numpy as np
from scipy import signal as sp_signal


def compute_fft(x, axis=0):
    """计算 FFT"""
    return np.fft.fftshift(np.fft.fft(x, axis=axis), axes=axis)


def compute_ifft(x, axis=0):
    """计算 IFFT"""
    return np.fft.ifft(np.fft.ifftshift(x, axes=axis), axis=axis)


def butter_lowpass(cutoff, fs, order=5):
    """设计 Butterworth 低通滤波器"""
    nyq = 0.5 * fs
    normal_cutoff = cutoff / nyq
    b, a = sp_signal.butter(order, normal_cutoff, btype="low", analog=False)
    return b, a


def apply_filter(data, cutoff, fs, order=5):
    """应用 Butterworth 低通滤波"""
    b, a = butter_lowpass(cutoff, fs, order=order)
    return sp_signal.filtfilt(b, a, data)


def kaiser_window(length, beta=8.0):
    """Kaiser 窗"""
    return np.kaiser(length, beta)


def hamming_window(length):
    """Hamming 窗"""
    return np.hamming(length)
