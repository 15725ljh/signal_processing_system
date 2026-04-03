/*
 * 模块01: 波形生成 (waveform_generation)
 *
 * 功能: 生成5种不同模式的雷达发射波形,用于后续干扰生成和信号处理
 *       本模块自动遍历所有模式(Case1~5),每种模式生成独立的波形信号矩阵
 *       所有结果保存到 output/ 目录
 *
 * 模式说明:
 *   Case1 - 固定跳频波形: 每个脉冲从N个频点中随机选择载频,增加抗干扰能力
 *   Case2 - 随机相位波形: 固定载频,每个脉冲附加随机相位,增加信号随机性
 *   Case3 - 脉冲重复间隔抖动波形: 随机改变脉冲间隔,使敌方难以捕捉规律
 *   Case4 - 混合波形(跳频+抖动): 结合跳频和脉冲抖动的优点
 *   Case5 - 跳频+随机相位复合波形: 结合跳频和随机相位,抗干扰能力最强
 *
 * 依赖: Module0.h(工具函数), Module1.h(波形生成函数), Config.h(配置管理)
 * 输入: config.json 中的 system.* 和 waveform.* 参数
 * 输出: output/boxing_Case*_Radar_Sig.dat, output/boxing_Case*_fre.dat 等
 */
#include "Module0.h"   // 工具函数(promptToExit等) + 全局参数
#include "Module1.h"   // 波形生成函数(boxing + Case1~5)
#include "Config.h"    // 配置管理器(加载config.json外部参数)

int main() {
    Config::instance().load();
    boxing();
    promptToExit();
    return 0;
}
