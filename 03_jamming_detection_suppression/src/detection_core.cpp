/*
 * detection_core.cpp — 模块03 GUI 薄封装实现
 *
 * 流程完全复刻 main.cpp, 但额外收集中间数据用于 GUI 可视化:
 *   1. loadFromFile(config_path) 注入 GUI 参数
 *   2. generateEcho() 生成含干扰回波
 *   3. TfrStft::tfrstft() + jamLocated() 获取 STFT 和干扰掩码 (可视化用)
 *   4. grDetection() 逐 CPI 识别干扰类型
 *   5. countDuplicateVectors() 统计识别结果
 *   6. jamTarDivi() 分离干扰和目标
 *   7. 计算 JSR
 *
 * 不修改任何现有头文件, 仅调用已有函数。
 */
#include "detection_core.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <iomanip>

#include <Eigen/Dense>

#include "Config.h"
#define CFG Config::instance()
#include "EchoGenerator.h"
#include "GrDetection.h"
#include "JamTarDivi.h"
#include "CountDuplicateVectors.h"

using namespace Eigen;

// NullBuffer: 用于抑制 countDuplicateVectors 的日志输出
class NullBuffer : public std::streambuf {
public:
    int overflow(int c) override { return c; }
};

DetectionResult run_detection(int realLabel, const std::string& config_path) {
    DetectionResult result;

    auto t_start = std::chrono::high_resolution_clock::now();

    // ── 1. 加载 GUI 注入的临时配置 ──
    if (!Config::instance().loadFromFile(config_path)) {
        result.log = "[ERROR] 无法加载配置文件: " + config_path;
        return result;
    }

    // ── 2. 读取参数 ──
    const double B = CFG.getDouble("detection_suppression.B", 80e6);
    const double Tp = CFG.getDouble("detection_suppression.Tp", 12e-6);
    const int cpiNum = CFG.getInt("detection_suppression.cpiNum", 100);
    const int nrn = CFG.getInt("detection_suppression.nrn", 2048);
    const int STFT_NUM = CFG.getInt("detection_suppression.gr_stft_num", 256);
    const int hamming_len = CFG.getInt("detection_suppression.gr_hamming_len", 63);

    result.cpiNum = cpiNum;
    result.nrn = nrn;

    // ── 3. 生成回波 (复用 EchoGenerator.h) ──
    EchoGeneratorResult echoResult = generateEcho(cpiNum, realLabel);
    result.echo_signal = echoResult.echo_signal;
    result.s_echo_noise = echoResult.s_echo_noise;

    // ── 4. STFT + 干扰定位 (用于可视化) ──
    // 复用 GrDetection.h 中的 hamming_window_gr 函数
    VectorXd t_axis = VectorXd::LinSpaced(nrn, 1, nrn);
    VectorXd hamming = hamming_window_gr(hamming_len);

    result.stft_matrix = TfrStft::tfrstft(
        result.echo_signal.row(0).transpose(), t_axis, STFT_NUM, hamming);
    result.jam_mask = jamLocated(result.stft_matrix);

    // ── 5. 逐 CPI 识别 (复用 GrDetection.h) ──
    result.detection_types.resize(cpiNum);
    for (int i = 0; i < cpiNum; ++i) {
        result.detection_types(i) = grDetection(B, Tp, result.echo_signal.row(i));
    }

    // ── 6. 统计识别结果 (复用 CountDuplicateVectors.h) ──
    NullBuffer null_buf;
    std::ostream null_stream(&null_buf);
    VectorCountResult stats = countDuplicateVectors(result.detection_types, 2, null_stream);

    if (stats.uniqueVectors.rows() > 0) {
        result.dominant_type = stats.uniqueVectors(0, 0);
        result.correct_count = (result.dominant_type != 0) ? stats.counts(0) : 0;
    }

    // ── 7. 干扰分离 (复用 JamTarDivi.h) ──
    JamTarDiviResult jamResult = jamTarDivi(result.echo_signal.row(0).transpose());
    result.jamming_signal = jamResult.jammingsignal;
    result.target_signal = jamResult.targetsignal;

    // ── 8. 计算 JSR (复刻 main.cpp 公式) ──
    // JSR = 20*log10( (max|target|/max|jam|) / (max|s_echo_noise|/max|Echo.row(0)|) )
    double max_echo = result.echo_signal.row(0).cwiseAbs().maxCoeff();
    double max_noise_echo = result.s_echo_noise.cwiseAbs().maxCoeff();
    double max_jam = jamResult.jammingsignal.cwiseAbs().maxCoeff();
    double max_tgt = jamResult.targetsignal.cwiseAbs().maxCoeff();

    double kkk = (max_echo > 1e-14) ? (max_noise_echo / max_echo) : 0.0;
    double kkk1 = (max_jam > 1e-14) ? (max_tgt / max_jam) : 0.0;

    if (kkk > 1e-14 && kkk1 > 1e-14) {
        result.jsr = 20.0 * std::log10(kkk1 / kkk);
    } else {
        result.jsr = 0.0;
    }

    // ── 9. 计时 + 组装日志 ──
    auto t_end = std::chrono::high_resolution_clock::now();
    result.elapsed = std::chrono::duration<double>(t_end - t_start).count();

    const char* typeNames[] = {"无干扰", "ISDJ/ISRJ/ISCJ", "RDJ", "NBJ"};
    int expectedType(int rl);
    auto expType = (realLabel <= 3) ? 1 : (realLabel == 4 ? 3 : 2);

    std::ostringstream oss;
    oss << std::fixed;
    oss << "RealLabel=" << realLabel
        << " | 识别=" << result.dominant_type
        << " (" << typeNames[result.dominant_type] << ")"
        << " | 正确=" << result.correct_count << "/" << cpiNum
        << std::setprecision(2)
        << " | JSR干扰抑制比=" << result.jsr << " dB"
        << std::setprecision(3)
        << " | 耗时=" << result.elapsed << "s";

    if (result.dominant_type == expType) {
        oss << " [正确]";
    } else {
        oss << " [错误, 期望=" << expType << "]";
    }

    result.log = oss.str();

    return result;
}
