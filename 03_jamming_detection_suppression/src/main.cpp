/*
 * 模块03: 干扰识别与抑制 (jamming_detection_suppression)
 *
 * 功能: 对5种干扰类型(无干扰/ISRJ/RDJ/NBJ/多假目标)进行自动识别和抑制
 *       本模块自动遍历所有类型(RealLabel 1~5),每种类型生成100个CPI进行统计
 *       识别基于STFT时频分析+Otsu阈值+频谱特征提取
 *       抑制基于JamTarDivi(Tsallis交叉熵时频分割)
 *
 * 流程:
 *   1. generateEcho() - 生成含特定干扰类型的仿真回波信号
 *   2. grDetection() - 对每个CPI进行干扰类型识别(时频分析+特征判别)
 *   3. countDuplicateVectors() - 统计识别结果,取众数作为最终判断
 *   4. jamTarDivi() - 对第一个脉冲执行干扰解耦(分离干扰和目标信号)
 *   5. 计算 ISR(干扰抑制比) = 20*log10(抑制后干信比/抑制前干信比)
 *
 * 依赖: EchoGenerator.h, GrDetection.h, JamTarDivi.h, CountDuplicateVectors.h,
 *       SignalWriter.h, Config.h
 * 输入: config.json 中的 detection_suppression.* 参数(fc=35GHz独立参数体系)
 * 输出: output/03_detection_识别与抑制日志_log.txt(结果汇总)
 *       output/03_detection_typeN_分离信号_signals.txt(信号详情)
 */
#include <iostream>            // 标准输入输出(cout, cerr)
#include <fstream>             // 文件输出(ofstream,写日志)
#include <vector>              // STL动态数组(存储结果)
#include <complex>             // 复数类型(Eigen::MatrixXcd)
#include <string>              // STL字符串
#include <chrono>              // C++11高精度计时(high_resolution_clock)
#include <iomanip>             // I/O格式控制(setprecision, setw等)
#include <sstream>             // 字符串流
#include <filesystem>          // C++17文件系统(创建输出目录)
#include <Eigen/Dense>          // Eigen线性代数库(Matrix, Vector等)
#include "Config.h"            // 配置管理器(读取detection_suppression.*参数)
#define CFG Config::instance() // 配置管理器单例快捷宏
#include "EchoGenerator.h"     // 回波信号生成器(生成含特定干扰类型的仿真回波)
#include "GrDetection.h"       // 干扰类型识别(STFT+Otsu+频谱特征)
#include "JamTarDivi.h"        // 干扰解耦(Tsallis交叉熵时频分割)
#include "CountDuplicateVectors.h" // 重复向量统计(取众数作为最终识别结果)
#include "SignalWriter.h"      // 信号写入文件(保存分离后的干扰/目标信号)

#ifndef OUTPUT_DIR
#define OUTPUT_DIR "../output/" // 统一输出目录
#endif

namespace std_fs = std::filesystem;

class NullBuffer : public std::streambuf { public: int overflow(int c) override { return c; } };
static NullBuffer null_buf;
static std::ostream null_stream(&null_buf);

const char* typeNames[] = {"无干扰", "ISDJ/ISRJ/ISCJ", "RDJ", "NBJ"};
int expectedType(int realLabel) { return (realLabel <= 3) ? 1 : (realLabel == 4 ? 3 : 2); }

int main() {
    Config::instance().load();

    try { std_fs::create_directories(OUTPUT_DIR); } catch (...) {}
    // logFile: ofstream, 文本文件, 存储识别与抑制结果汇总日志
    std::string logPath = std::string(OUTPUT_DIR) + "03_detection_识别与抑制日志_log.txt";
    std::ofstream logFile(SW_U8PATH(logPath));
    if (!logFile.is_open()) {
        std::cerr << "错误：无法打开日志文件 " << logPath << " 进行写入。\n";
        return 1;
    }

    logFile << std::fixed;

    auto t_total_start = std::chrono::high_resolution_clock::now();

    const double B = CFG.getDouble("detection_suppression.B", 80e6);
    const double Tp = CFG.getDouble("detection_suppression.Tp", 12e-6);
    const int CpiNum = CFG.getInt("detection_suppression.cpiNum", 100);

    struct Result { int realLabel; int detected; int correct; double isr; double time_s; };
    std::vector<Result> results;

    logFile << "==================== 干扰识别与抑制测试 ====================\n";
    logFile << "参数: B=80MHz, Tp=12us, CpiNum=" << CpiNum << "\n\n";

    for (int currentRealLabel = 1; currentRealLabel <= 5; ++currentRealLabel) {
        auto t_start = std::chrono::high_resolution_clock::now();

        EchoGeneratorResult echoResult = generateEcho(CpiNum, currentRealLabel);
        Eigen::MatrixXcd Echo = echoResult.echo_signal;
        Eigen::RowVectorXcd s_echo_noise = echoResult.s_echo_noise;

        Eigen::VectorXi J_type(CpiNum);
        for (int num = 0; num < CpiNum; ++num) {
            J_type(num) = grDetection(B, Tp, Echo.row(num));
        }

        VectorCountResult uniqueResult = countDuplicateVectors(J_type, 2, null_stream);
        int dominant_type = (uniqueResult.uniqueVectors.rows() > 0) ? uniqueResult.uniqueVectors(0, 0) : 0;
        int correct_count = (dominant_type != 0) ? uniqueResult.counts(0) : 0;

        JamTarDiviResult jamResult = jamTarDivi(Echo.row(0).transpose());

        double kkk = s_echo_noise.cwiseAbs().maxCoeff() / Echo.row(0).cwiseAbs().maxCoeff();
        double kkk1 = jamResult.targetsignal.cwiseAbs().maxCoeff() / jamResult.jammingsignal.cwiseAbs().maxCoeff();
        double ISR = 20.0 * std::log10(kkk1 / kkk);

        // filename: txt文本, 包含4组信号(含噪目标回波/纯干扰/分离干扰/分离目标, 均为RowVectorXcd 1×nrn)
        std::string filename = std::string(OUTPUT_DIR) + "03_detection_type" + std::to_string(currentRealLabel) + "_分离信号_signals.txt";
        writeConsolidatedSignalsToFile(s_echo_noise, Echo.row(0)-s_echo_noise, jamResult.jammingsignal, jamResult.targetsignal, filename, null_stream);

        auto t_end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t_end - t_start).count();

        results.push_back({currentRealLabel, dominant_type, correct_count, ISR, elapsed});

        logFile << "RealLabel=" << currentRealLabel
                 << " | 识别=" << dominant_type << " (" << typeNames[dominant_type] << ")"
                 << " | 正确=" << correct_count << "/" << CpiNum
                 << std::setprecision(2) << " | ISR=" << ISR << " dB"
                 << std::setprecision(3) << " | 耗时=" << elapsed << "s"
                 << " | " << filename << "\n";
    }

    auto t_total_end = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(t_total_end - t_total_start).count();

    logFile << "\n==================== 汇总 ====================\n";
    logFile << "RealLabel | 识别结果 | 类型名称       | 正确率   | ISR(dB)  | 耗时(s)\n";
    logFile << "----------|----------|----------------|----------|----------|--------\n";
    int total_correct = 0;
    for (const auto& r : results) {
        int exp = expectedType(r.realLabel);
        int correct = (r.detected == exp) ? r.correct : 0;
        total_correct += correct;
        logFile << "    " << r.realLabel << "     |    " << r.detected
                 << "     | " << std::left << std::setw(14) << typeNames[r.detected]
                 << " | " << std::right << std::setw(3) << correct << "/" << CpiNum
                 << "  | " << std::setw(7) << std::setprecision(2) << r.isr
                 << " | " << std::setprecision(3) << r.time_s << "\n";
    }
    logFile << "----------|----------|----------------|----------|----------|--------\n";
    logFile << "总正确: " << total_correct << "/" << CpiNum*5
             << std::setprecision(2) << " | 总耗时: " << total_time << "s\n";
    logFile << "==================================================\n";

    logFile.close();
    return 0;
}
