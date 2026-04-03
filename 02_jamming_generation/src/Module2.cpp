/*
 * 模块02 - 干扰生成实现 (ganrao)
 *
 * 功能: 自动遍历10种干扰生成模式(Case1~10),每种模式独立生成干扰信号
 *       叠加到雷达回波上形成含干扰的总回波信号
 *
 * 注意: Case1~4 使用全零 Radar_Sig 输入(仅生成干扰);
 *       Case5~10 在函数内部同时生成目标回波和干扰信号
 *
 * 输出: output/02_jamming_CaseN_含干扰回波_jammed.dat (复数矩阵, nrn x nan1)
 *       output/02_jamming_CaseN_干扰类型缩写.dat (纯干扰信号, 各Case独立命名)
 */
#include "Module0.h"   // 工具函数(saveMatrix等) + 全局参数
#include "Module2.h"   // 干扰生成函数声明(ganrao + Case1~10)

int ganrao() {    
    init_tnrn_and_fr(tnrn, fr);
    Radar_Sig = MatrixXcd::Zero(nrn(), nan1());
    win = VectorXd::Zero(nrn());

    const char* modeNames[] = {"", "距离假目标干扰(RDJ)", "速度假目标干扰(VDJ)", "间歇采样转发干扰(ISRJ)",
        "窄带噪声干扰(NNJ)", "距离波门拖引干扰(RGPOJ)", "速度波门拖引干扰(VGPOJ)",
        "密集假目标干扰(DRFTJ)", "脉内前沿切片重复干扰(IPLESRJ)", "频谱弥散干扰(SMSPJ)", "梳状谱干扰(COMBJ)"};

    for (int mode = 1; mode <= 10; ++mode) {
        cout << "\n========== 模式" << mode << ": " << modeNames[mode] << " ==========" << endl;
        ganrao_mode = mode;

        MatrixXcd echo_target = MatrixXcd::Zero(nrn(), nan1());
        MatrixXcd Jam_Sig = MatrixXcd::Zero(nrn(), nan1());

        switch (mode) {
            case 1:  ganrao_Case1(Radar_Sig, echo_target, Jam_Sig);  break;
            case 2:  ganrao_Case2(Radar_Sig, echo_target, Jam_Sig);  break;
            case 3:  ganrao_Case3(Radar_Sig, echo_target, Jam_Sig);  break;
            case 4:  ganrao_Case4(Radar_Sig, echo_target, Jam_Sig);  break;
            case 5:  ganrao_Case5(Radar_Sig, echo_target, Jam_Sig);  break;
            case 6:  ganrao_Case6(Radar_Sig, echo_target, Jam_Sig);  break;
            case 7:  ganrao_Case7(Radar_Sig, echo_target, Jam_Sig);  break;
            case 8:  ganrao_Case8(Radar_Sig, echo_target, Jam_Sig);  break;
            case 9:  ganrao_Case9(Radar_Sig, echo_target, Jam_Sig);  break;
            case 10: ganrao_Case10(Radar_Sig, echo_target, Jam_Sig); break;
        }

        Radar_Sig = echo_target;
        cout << "Radar_Sig(360,32) = " << Radar_Sig(360,32) << endl;
        // Radar_Sig: MatrixXcd, 维度 nrn×nan1, 含干扰回波信号(复数,原始信号+干扰叠加)
        saveMatrix("02_jamming_Case" + to_string(mode) + "_含干扰回波_jammed.dat", Radar_Sig);
        cout << "模式" << mode << " 完成,已保存" << endl;
    }

    return 0;
}
