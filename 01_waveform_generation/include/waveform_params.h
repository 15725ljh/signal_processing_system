#ifndef WAVEFORM_PARAMS_H
#define WAVEFORM_PARAMS_H

// 波形生成参数结构体 — 不依赖 Config.h / parameters.h / Module0.h
// 所有参数通过构造函数或直接赋值传入,实现完全解耦

struct WaveformParams {
    // ── 系统参数 ──
    double fc    = 16e9;     // 载波频率 (Hz)
    double Tp    = 12e-6;    // 脉冲宽度 (s)
    double B     = 40e6;     // 信号带宽 (Hz)
    double prf   = 10e3;     // 脉冲重复频率 (Hz)
    double Vr    = 50.0;     // 相对径向速度 (m/s)
    double Rs    = 10000.0;  // 场景中心斜距 (m)
    double wr    = 608.0;    // 场景距离向宽度 (m)
    int    nan1  = 64;       // 方位向脉冲数

    // ── Case1: 固定跳频 ──
    int    case1_N       = 10;       // 跳频点数
    double case1_delta_f = 40e6;     // 频率步进 (Hz), 默认=B

    // ── Case3: PRI抖动 ──
    double case3_prt       = 1000e-6;  // 标称脉冲重复间隔 (s)
    double case3_amp       = 1.0;      // 信号幅度
    int    case3_jitter_us = 20;       // 抖动范围 (μs)

    // ── Case4: 混合(跳频+抖动) ──
    double case4_delta_f   = 40e6;     // 频率步进 (Hz)
    int    case4_fcnum     = 16;       // 载频分组数
    double case4_amp       = 1.0;      // 信号幅度
    double case4_prt       = 1000e-6;  // 标称脉冲重复间隔 (s)
    int    case4_jitter_us = 20;       // 抖动范围 (μs)

    // ── Case5: 跳频+随机相位 ──
    int    case5_N       = 10;       // 跳频点数
    double case5_delta_f = 40e6;     // 频率步进 (Hz)
};

#endif // WAVEFORM_PARAMS_H
