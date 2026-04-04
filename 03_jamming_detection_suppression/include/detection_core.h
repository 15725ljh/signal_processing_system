/*
 * detection_core.h — 模块03 GUI 薄封装 API
 *
 * 提供检测+分离+ISR评估的完整流水线, 供 03_GUI_detection 通过 pybind11 调用。
 * 所有算法逻辑复用现有头文件 (EchoGenerator.h, GrDetection.h, JamTarDivi.h 等),
 * 本文件仅负责组装流水线并收集中间结果用于可视化。
 *
 * 参数注入方式: Python 端将参数写入临时 JSON → loadFromFile() → 调用现有函数
 */
#ifndef DETECTION_CORE_H
#define DETECTION_CORE_H

#include <Eigen/Dense>
#include <string>

/**
 * @brief 检测+分离完整结果
 */
struct DetectionResult {
    Eigen::MatrixXcd echo_signal;      // (cpiNum × nrn) 完整混合回波矩阵
    Eigen::RowVectorXcd s_echo_noise;  // (1 × nrn) 原始带噪目标回波
    Eigen::MatrixXcd stft_matrix;      // (STFT_NUM × nrn) 第0个脉冲的 STFT 时频图
    Eigen::MatrixXd jam_mask;          // (STFT_NUM × nrn) Otsu 干扰定位二值掩码
    Eigen::VectorXi detection_types;   // (cpiNum) 每个CPI的识别结果
    Eigen::VectorXcd jamming_signal;   // (nrn) 分离出的干扰信号
    Eigen::VectorXcd target_signal;    // (nrn) 分离出的目标信号
    int dominant_type = 0;             // 最终识别结果 (众数)
    int correct_count = 0;             // 投票给 dominant_type 的 CPI 数
    int cpiNum = 0;                    // CPI 数量
    int nrn = 0;                       // 距离向采样点数
    double jsr = 0.0;                  // 干扰抑制比 (dB)
    double elapsed = 0.0;              // 计算耗时 (s)
    std::string log;                   // C++ 日志输出
};

/**
 * @brief 运行干扰检测+分离流水线
 * @param realLabel 干扰类型标签 (1: ISDJ, 2: ISRJ, 3: ISCJ, 4: NBJ, 5: RDJ)
 * @param config_path 包含 detection_suppression.* 参数的 JSON 配置文件路径
 * @return DetectionResult 完整检测结果
 */
DetectionResult run_detection(int realLabel, const std::string& config_path);

#endif // DETECTION_CORE_H
