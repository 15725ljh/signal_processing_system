/*
 * 模块01 - 波形生成实现 (boxing)
 *
 * 功能: 初始化时间/频率向量,打印系统参数,自动遍历5种波形生成模式
 *       每种模式调用共享库 waveform_core 生成信号,再保存到 output/ 目录
 *
 * 输出: output/01_waveform_CaseN_信号矩阵_signal.dat (复数矩阵, nrn x nan1)
 *       output/01_waveform_CaseN_跳频序列_freq_hop.dat (Case1/5, 实向量, nan1 x 1)
 *       output/01_waveform_CaseN_随机相位序列_random_phase.dat (Case2/5, 复向量, nan1 x 1)
 *       output/01_waveform_CaseN_等效载频序列_freq_seq.dat (Case3/4, 实向量, nan1 x 1)
 */
#include "Module0.h"       // 工具函数(saveMatrix/print_f_phi1等) + 全局参数
#include "Module1.h"       // 波形生成函数声明(boxing)
#include "waveform_core.h" // 共享库: generate_waveform()

// 从 Config 构建 WaveformParams
static WaveformParams build_params() {
    WaveformParams p;
    p.fc  = fc();   p.Tp  = Tp();   p.B   = B();
    p.prf = prf();  p.Vr  = Vr();   p.Rs  = Rs();
    p.wr  = wr();   p.nan1 = nan1();

    // Case 1
    p.case1_N       = CFG.getInt("waveform.case1_freq_hop.N", 10);
    p.case1_delta_f = CFG.getDouble("waveform.case1_freq_hop.delta_f", B());

    // Case 3
    p.case3_prt       = CFG.getDouble("waveform.case3_pri_jitter.prt", 1000e-6);
    p.case3_amp       = CFG.getDouble("waveform.case3_pri_jitter.amp", 1.0);
    p.case3_jitter_us = CFG.getInt("waveform.case3_pri_jitter.jitter_us", 20);

    // Case 4
    p.case4_delta_f   = CFG.getDouble("waveform.case4_hybrid.delta_f", B());
    p.case4_fcnum     = CFG.getInt("waveform.case4_hybrid.fcnum", 16);
    p.case4_amp       = CFG.getDouble("waveform.case4_hybrid.amp", 1.0);
    p.case4_prt       = CFG.getDouble("waveform.case4_hybrid.prt", 1000e-6);
    p.case4_jitter_us = CFG.getInt("waveform.case4_hybrid.jitter_us", 20);

    // Case 5
    p.case5_N       = CFG.getInt("waveform.case5_combined.N", 10);
    p.case5_delta_f = CFG.getDouble("waveform.case5_combined.delta_f", B());

    return p;
}

int boxing() {
    init_tnrn_and_fr(tnrn, fr);
    win = VectorXd::Zero(nrn());

    cout << "*****************************************系统参数*******************************************" << endl;
    cout << "载波频率 fc: " << fc() << endl;
    cout << "脉冲宽度 Tp: " << Tp() << endl;
    cout << "信号带宽 B: " << B() << endl;
    cout << "采样频率 fs: " << fs() << endl;
    cout << "线性调频率 gama: " << gama() << endl;
    cout << "脉冲重复频率 prf: " << prf() << endl;
    cout << "波长 lambda: " << lambda() << endl;
    cout << "雷达与目标的相对径向速度 Vr: " << Vr() << endl;
    cout << "场景中心斜距 Rs: " << Rs() << endl;
    cout << "场景距离向宽度 wr: " << wr() << endl;
    cout << "干扰幅度增益 A_RJ: " << A_RJ() << endl;
    cout << "分贝转线性幅度倍数 amp_j: " << amp_j() << endl;
    cout << "距离向采样点数 nrn: " << nrn() << endl;
    cout << "方位向脉冲数 nan1: " << nan1() << endl;
    cout << "雷达的初始位置 (x_R0,y_R0,z_R0): " << x_R0 << ", " << y_R0 << ", " << z_R0() << endl;
    cout << "目标的反射系数 amp: " << amp << endl;
    cout << "目标点数 point_num: " << point_num << endl;
    cout << "距离向采样间隔 Tnrn: " << Tnrn() << endl;
    cout << "距离向采样起始时间 Tstart: "  << Tstart() << endl;
    cout << "距离向采样结束时间 Tend: " << Tend() << endl;
    cout << "距离向时间向量 tnrn 的某一元素 tnrn(1024): " << tnrn(1024) << endl;
    cout << "距离向频率向量 fr 的某一元素 fr(1001): " << fr(1001) << endl;
    cout << "波形信号矩阵 Radar_Sig 大小 : " << nrn() << " x " << nan1() << endl;
    cout << "*******************************************************************************************" << endl;

    const char* modeNames[] = {"", "固定跳频波形", "随机相位波形", "脉冲重复间隔抖动波形", "混合波形(跳频+抖动)", "跳频+随机相位复合波形"};

    WaveformParams params = build_params();

    for (int mode = 1; mode <= 5; ++mode) {
        cout << "\n========== 模式" << mode << ": " << modeNames[mode] << " ==========" << endl;
        boxing_mode = mode;

        // 调用共享库生成波形
        WaveformResult result = generate_waveform(mode, params);

        // 打印参数日志
        cout << result.log;

        // 保存信号矩阵
        Radar_Sig = result.radar_sig;
        cout << "Radar_Sig(360,32) = " << Radar_Sig(360, 32) << endl;
        saveMatrix("01_waveform_Case" + to_string(mode) + "_信号矩阵_signal.dat", Radar_Sig);

        // 保存附加序列
        if (result.has_f)
            saveVector("01_waveform_Case" + to_string(mode) + "_跳频序列_freq_hop.dat", result.f);
        if (result.has_phi1)
            saveVector("01_waveform_Case" + to_string(mode) + "_随机相位序列_random_phase.dat", result.phi1);
        if (result.has_freq_seq)
            saveVector("01_waveform_Case" + to_string(mode) + "_等效载频序列_freq_seq.dat", result.freq_seq);

        cout << "模式" << mode << " 完成,已保存" << endl;
    }

    return 0;
}
