#ifndef JAMMING_PARAMS_H
#define JAMMING_PARAMS_H

// 干扰生成参数结构体 — 不依赖 Config.h / parameters.h / Module0.h
// 所有参数通过构造函数或直接赋值传入,实现完全解耦

struct JammingParams {
    // ── 系统参数 (10个Case共享) ──
    double fc    = 16e9;     // 载波频率 (Hz)
    double Tp    = 12e-6;    // 脉冲宽度 (s)
    double B     = 40e6;     // 信号带宽 (Hz)
    double prf   = 10e3;     // 脉冲重复频率 (Hz)
    double Vr    = 50.0;     // 雷达-目标径向速度 (m/s)
    double Rs    = 10000.0;  // 场景中心斜距 (m)
    double wr    = 608.0;    // 场景距离向宽度 (m)
    double A_RJ  = 10.0;     // 干扰幅度增益 (dB)
    double z_R0  = 2000.0;   // 雷达初始高度 (m)
    int    nan1  = 64;       // 方位向脉冲数
    double range_walk_factor = 4000.0;  // 距离走动放大因子 (Case 4, 5)

    // ── Case 1: 距离假目标干扰 (RDJ) ──
    int    case1_jj         = 1;        // 干扰脉冲延迟数
    double case1_Rj         = 100.0;    // 干扰目标距离 (m)
    double case1_amp_j      = 10.0;     // 干扰信号幅度增益 (线性倍数)
    double case1_amp_target = 1.0;      // 目标幅度
    double case1_awgn_snr   = 10.0;     // AWGN 信噪比 (dB)

    // ── Case 2: 速度假目标干扰 (VDJ) ──
    double case2_Vj         = 1e5;      // 假目标速度 (m/s)
    int    case2_jj         = 1;        // 干扰延迟脉冲数
    double case2_Rj         = 10.0;     // 假目标距离 (m)
    double case2_amp_j      = 10.0;     // 干扰信号幅度增益 (线性倍数)
    double case2_amp_target = 1.0;      // 目标幅度
    double case2_awgn_snr   = 10.0;     // AWGN 信噪比 (dB)

    // ── Case 3: 间歇采样转发干扰 (ISRJ) ──
    double case3_Ts_ISRJ    = 4e-6;     // 采样周期 (s)
    double case3_T_ISRJ     = 0.0;      // 采样脉宽 (s), 0=默认 Ts_ISRJ/4
    double case3_Rj         = 10.0;     // 干扰目标距离 (m)
    double case3_amp_j      = 10.0;     // 干扰信号幅度增益 (线性倍数)
    double case3_amp_target = 1.0;      // 目标幅度
    double case3_awgn_snr   = 10.0;     // AWGN 信噪比 (dB)

    // ── Case 4: 窄带噪声干扰 (NNJ) ──
    double case4_power_dBW     = 20.0;  // 噪声功率 (dBW)
    int    case4_butter_order  = 8;     // 低通滤波器阶数
    double case4_butter_cutoff = 0.3;   // 归一化截止频率
    double case4_amp_target    = 1.0;   // 目标幅度
    double case4_awgn_snr      = 10.0;  // AWGN 信噪比 (dB)

    // ── Case 5: 距离波门拖引干扰 (RGPO) ──
    double case5_Vj           = 340.0;  // 假目标拖引速度 (m/s)
    double case5_amp_target   = 1.0;    // 目标幅度
    double case5_amp_jammer   = 1.4;    // 干扰幅度
    int    case5_drag_stages  = 4;      // 拖引阶段次数
    double case5_awgn_snr     = 10.0;   // AWGN 信噪比 (dB)

    // ── Case 6: 速度波门拖引干扰 (VGPO) ──
    double case6_Vj           = 1e4;    // 假目标速度 (m/s)
    double case6_amp_target   = 1.0;    // 目标幅度
    double case6_amp_jammer   = 1.4;    // 干扰幅度
    int    case6_drag_stages  = 4;      // 拖引阶段次数
    double case6_awgn_snr     = 10.0;   // AWGN 信噪比 (dB)
    double case6_velocity_drag_factor = 40000.0;  // 速度拖引放大因子

    // ── Case 7: 密集假目标干扰 (DRFTJ) ──
    double case7_JSR        = 0.0;      // 干信比 (dB)
    double case7_amp_target = 1.0;      // 目标幅度
    int    case7_num_jam    = 50;       // 转发次数
    double case7_detaR      = 50.0;     // 每次转发距离增量 (m)
    int    case7_forward_replicas = 3;  // 前向转发次数 (jn < 此值 → 前向)
    double case7_awgn_snr   = 10.0;     // AWGN 信噪比 (dB)

    // ── Case 8: 脉内前沿切片重复干扰 (IPLESRJ) ──
    double case8_A_RJ         = 30.0;   // 干扰机发射功率增益 (dB)
    double case8_amp_target   = 1.0;    // 目标幅度
    double case8_R0_new       = 608.0;  // 雷达-目标初始距离 (m), 独立参数
    double case8_V_ISRJ       = 0.0;    // 雷达-干扰机径向速度 (m/s)
    int    case8_T_ISRJ_ratio = 16;     // 干扰间隔 = Tp / 此值
    double case8_R_ahead      = 0.0;    // 干扰机提前发射时间 (s)
    double case8_awgn_snr     = 10.0;   // AWGN 信噪比 (dB)

    // ── Case 9: 频谱弥散干扰 (SMSP) ──
    int    case9_num_slices = 4;        // 频谱切片次数
    double case9_JSR        = 15.0;     // 干信比 (dB)
    double case9_amp_target = 1.0;      // 目标幅度
    double case9_amp_extra  = 1.4;      // 干扰幅度额外系数
    double case9_R0         = 10000.0;  // 目标初始距离 (m)
    double case9_awgn_snr   = 10.0;     // AWGN 信噪比 (dB)

    // ── Case 10: 梳状谱干扰 (COMB) ──
    int    case10_num_tones  = 7;       // 频谱线数量
    double case10_JSR        = 0.0;     // 干信比 (dB)
    double case10_amp_target = 1.0;     // 目标幅度
    double case10_deltaf     = 1e6;     // 频率间隔 (Hz)
    double case10_R0         = 10000.0; // 目标初始距离 (m)
    double case10_awgn_snr   = 10.0;    // AWGN 信噪比 (dB)
};

#endif // JAMMING_PARAMS_H
