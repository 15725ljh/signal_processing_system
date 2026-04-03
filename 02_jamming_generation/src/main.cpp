/*
 * 模块02: 干扰生成 (jamming_generation)
 *
 * 功能: 生成10种不同模式的电子干扰信号,叠加到雷达回波上
 *       本模块自动遍历所有模式(Case1~10),每种模式独立生成干扰
 *       所有结果保存到 output/ 目录
 *
 * 模式说明:
 *   Case1  - 距离假目标干扰(RDJ): 通过频域相位调制模拟不同距离假目标
 *   Case2  - 速度假目标干扰(VDJ): 通过多普勒频移模拟虚假速度目标
 *   Case3  - 间歇采样转发干扰(ISRJ): 周期性采样和切片重组产生多个假目标
 *   Case4  - 窄带噪声干扰(NNJ): 带限噪声降低接收机信噪比
 *   Case5  - 距离波门拖引干扰(RGPOJ): 渐进改变假目标距离诱骗雷达跟踪
 *   Case6  - 速度波门拖引干扰(VGPOJ): 渐进改变假目标速度诱骗速度跟踪
 *   Case7  - 密集假目标干扰(DRFTJ): 距离维密集转发形成假目标群
 *   Case8  - 脉内前沿切片重复干扰(IPLESRJ): 对前沿切片多次重复转发
 *   Case9  - 频谱弥散干扰(SMSPJ): 子脉冲调频率增强产生频谱弥散
 *   Case10 - 梳状谱干扰(COMBJ): 多离散频率点产生梳状谱假目标
 *
 * 注意: Case1~4 使用全零 Radar_Sig 作为输入(仅生成干扰);
 *       Case5~10 在函数内部同时生成目标回波和干扰信号
 * 依赖: Module0.h(工具函数), Module2.h(干扰生成函数), Config.h(配置管理)
 * 输出: output/ganrao_Case*_Radar_Sig(jammed).dat 等
 */
#include "Module0.h"   // 工具函数(promptToExit等) + 全局参数
#include "Module2.h"   // 干扰生成函数(ganrao + Case1~10)
#include "Config.h"    // 配置管理器(加载config.json外部参数)

int main() {
    Config::instance().load();
    ganrao();
    promptToExit();
    return 0;
}
