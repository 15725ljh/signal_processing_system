/*
 * signal_processing_core.h — 模块04 GUI 薄封装 API
 *
 * 提供三个独立入口供 04_GUI_signal_processing 通过 pybind11 调用:
 *   1. run_recognition()         — 干扰识别 (STFT + gr_detection → J_type 0-10)
 *   2. run_processing_rd()      — 距离-多普勒处理 (Cases 1-5, 输出 RD map)
 *   3. run_processing_decouple() — 时频干扰解耦 (Case 6, 输出分离信号 + JSR)
 *
 * 参数注入方式: Python 端将参数写入临时 JSON → loadFromFile() → 调用现有函数
 */
#ifndef SIGNAL_PROCESSING_CORE_H
#define SIGNAL_PROCESSING_CORE_H

#include <Eigen/Dense>
#include <string>

/**
 * @brief 干扰识别结果
 */
struct RecognitionResult {
    Eigen::MatrixXcd stft_matrix;      // (STFT_NUM × nrn) STFT 时频图
    Eigen::MatrixXd jam_mask;          // (STFT_NUM × nrn) Otsu 干扰定位二值掩码
    Eigen::VectorXcd echo_pulse;       // (nrn) 选中脉冲的混合回波
    int j_type = 0;                    // 识别结果 0-10
    int nrn = 0;
    double elapsed = 0;
    std::string log;
};

/**
 * @brief 距离-多普勒处理结果 (Cases 1-5)
 */
struct ProcessingResultRD {
    Eigen::MatrixXcd rd_map;           // (nrn × nan1) 距离-多普勒图
    Eigen::VectorXd xi;                // (nrn) 距离轴 (m)
    Eigen::VectorXd dv;                // (nan1) 速度轴 (m/s)
    Eigen::MatrixXcd input_signal;     // (nrn × nan1) 输入信号 (Radar_Sig)
    int nrn = 0, nan1 = 0, case_num = 0;
    double elapsed = 0;
    std::string log;
};

/**
 * @brief 时频干扰解耦结果 (Case 6)
 */
struct ProcessingResultDecouple {
    Eigen::MatrixXcd jam_signal;       // (nrn × nan1) 分离干扰信号
    Eigen::MatrixXcd target_signal;    // (nrn × nan1) 分离目标信号
    Eigen::VectorXd decouple_flag;     // (nan1) 解耦标志 (0=正常, 1=高阶谱)
    double jsr_dB = 0;                 // 干扰抑制比 (dB)
    double avg_threshold = 0;          // 平均 Tsallis 阈值
    int gaojiepu_count = 0;            // 高阶谱脉冲数
    Eigen::MatrixXcd input_signal;     // (nrn × nan1) 输入混合信号
    int nrn = 0, nan1 = 0;
    double elapsed = 0;
    std::string log;
};

/**
 * @brief 干扰识别: 生成含干扰回波 → STFT + gr_detection → J_type
 * @param jam_type    干扰类型 (1=ISDJ, 2=ISRJ, 3=ISCJ, 4=NBJ, 5=RDJ)
 * @param config_path 包含 system.*, recognition.*, detection_suppression.* 的 JSON 文件
 */
RecognitionResult run_recognition(int jam_type, const std::string& config_path);

/**
 * @brief 距离-多普勒处理: 生成波形 → 脉冲压缩 + 多普勒处理 → RD map
 * @param case_num    处理模式 (1-5)
 * @param config_path 包含 system.*, processing.*, waveform.* 的 JSON 文件
 */
ProcessingResultRD run_processing_rd(int case_num, const std::string& config_path);

/**
 * @brief 时频干扰解耦: 生成含干扰回波 → 逐脉冲 jamTarDivi → JSR
 * @param jam_type    干扰类型 (1-5)
 * @param config_path 包含 system.*, processing.*, detection_suppression.* 的 JSON 文件
 */
ProcessingResultDecouple run_processing_decouple(int jam_type, const std::string& config_path);

#endif // SIGNAL_PROCESSING_CORE_H
