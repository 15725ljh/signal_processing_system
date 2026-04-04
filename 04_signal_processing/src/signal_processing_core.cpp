/*
 * signal_processing_core.cpp — 模块04 GUI 薄封装实现
 *
 * 三个入口函数:
 *   1. run_recognition()         — 干扰识别 (复用 gr_detection)
 *   2. run_processing_rd()      — 距离-多普勒处理 (复用 chuli_Case1~5)
 *   3. run_processing_decouple() — 时频干扰解耦 (复用 jamTarDivi)
 *
 * 不修改任何现有头文件, 仅调用已有函数。
 */
#include "signal_processing_core.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <iomanip>

#include <Eigen/Dense>

#include "Config.h"
#define CFG Config::instance()
#include "Module0.h"              // tfrstft, JamLocated, init_tnrn_and_fr, fft, fftshift, saveMatrix
#include "Module2.5.h"            // gr_detection
#include "Module3.h"              // chuli_Case1~6
#include "EchoGenerator4.h"       // generateEcho4
#include "JamTarDivi.h"           // jamTarDivi
#include "waveform_core.h"        // generate_waveform (from Module01)

using namespace Eigen;
using namespace std;

// 干扰类型名称映射 (J_type 0-10)
static const char* J_TYPE_NAMES[] = {
    "无干扰",           // 0
    "常规梳状谱",       // 1
    "规则梳状谱",       // 2
    "随机梳状谱",       // 3
    "频谱弥散",         // 4
    "灵巧噪声",         // 5
    "窄带干扰",         // 6
    "密集假目标",       // 7
    "扫频干扰",         // 8
    "单间歇梳状谱",     // 9
    "联合拖引",         // 10
};

// ── 初始化全局变量 ──
static void init_globals() {
    init_tnrn_and_fr(tnrn, fr);
    // 频域滤波窗: |fr| <= B/2 时为 1
    int _nrn = nrn();
    double _B = B();
    win.resize(_nrn);
    for (int i = 0; i < _nrn; ++i) {
        win(i) = (abs(fr(i)) <= _B / 2.0) ? 1.0 : 0.0;
    }
}

// ── 从 Config 构建 WaveformParams (供 Cases 1-5 使用) ──
static WaveformParams build_waveform_params() {
    WaveformParams p;
    p.fc   = CFG.getDouble("system.fc",   16e9);
    p.Tp   = CFG.getDouble("system.Tp",   12e-6);
    p.B    = CFG.getDouble("system.B",    40e6);
    p.prf  = CFG.getDouble("system.prf",  10e3);
    p.Vr   = CFG.getDouble("system.Vr",   50.0);
    p.Rs   = CFG.getDouble("system.Rs",   10000.0);
    p.wr   = CFG.getDouble("system.wr",   608.0);
    p.nan1 = CFG.getInt("system.nan1",    64);

    p.case1_N       = CFG.getInt("waveform.case1_freq_hop.N", 10);
    p.case1_delta_f = CFG.getDouble("waveform.case1_freq_hop.delta_f", p.B);

    p.case3_prt       = CFG.getDouble("waveform.case3_pri_jitter.prt", 1000e-6);
    p.case3_amp       = CFG.getDouble("waveform.case3_pri_jitter.amp", 1.0);
    p.case3_jitter_us = CFG.getInt("waveform.case3_pri_jitter.jitter_us", 20);

    p.case4_delta_f   = CFG.getDouble("waveform.case4_hybrid.delta_f", p.B);
    p.case4_fcnum     = CFG.getInt("waveform.case4_hybrid.fcnum", 16);
    p.case4_amp       = CFG.getDouble("waveform.case4_hybrid.amp", 1.0);
    p.case4_prt       = CFG.getDouble("waveform.case4_hybrid.prt", 1000e-6);
    p.case4_jitter_us = CFG.getInt("waveform.case4_hybrid.jitter_us", 20);

    p.case5_N       = CFG.getInt("waveform.case5_combined.N", 10);
    p.case5_delta_f = CFG.getDouble("waveform.case5_combined.delta_f", p.B);

    return p;
}


// ═══════════════════════════════════════════════════════════════════
//  1. 干扰识别
// ═══════════════════════════════════════════════════════════════════

RecognitionResult run_recognition(int jam_type, const std::string& config_path) {
    RecognitionResult result;

    auto t_start = chrono::high_resolution_clock::now();

    // ── 1. 加载配置 ──
    if (!Config::instance().loadFromFile(config_path)) {
        result.log = "[ERROR] 无法加载配置文件: " + config_path;
        return result;
    }

    int _nrn = nrn();

    // ── 2. 初始化全局变量 ──
    init_globals();
    result.nrn = _nrn;

    // ── 3. 生成含干扰回波 (cpiNum=1, 单脉冲) ──
    EchoGeneratorResult4 echoResult = generateEcho4(1, jam_type);
    result.echo_pulse = echoResult.echo_signal.row(0).transpose();

    // ── 4. 计算 STFT + Otsu 干扰掩码 (用于 GUI 可视化) ──
    int STFT_NUM = CFG.getInt("recognition.stft_num", 256);
    int hamming_len = CFG.getInt("recognition.hamming_len", 63);
    VectorXd h = generateHammingWindow(hamming_len);
    VectorXd t_in = VectorXd::LinSpaced(_nrn, 1, _nrn);

    MatrixXcd tfr, jam_tfr;
    VectorXd t_out, f_axis;
    tfrstft(result.echo_pulse, t_in, STFT_NUM, h, false, tfr, t_out, f_axis);
    JamLocated(tfr, jam_tfr);

    result.stft_matrix = tfr;
    result.jam_mask = jam_tfr.real();

    // ── 5. 调用 gr_detection 进行分类 ──
    result.j_type = gr_detection(result.echo_pulse);

    // ── 6. 计时 + 日志 ──
    auto t_end = chrono::high_resolution_clock::now();
    result.elapsed = chrono::duration<double>(t_end - t_start).count();

    const char* jam_names[] = {"", "ISDJ", "ISRJ", "ISCJ", "NBJ", "RDJ"};
    const char* jam_name = (jam_type >= 1 && jam_type <= 5) ? jam_names[jam_type] : "?";
    const char* jtype_name = (result.j_type >= 0 && result.j_type <= 10) ? J_TYPE_NAMES[result.j_type] : "?";

    ostringstream oss;
    oss << fixed << setprecision(3);
    oss << "干扰类型=" << jam_type << "(" << jam_name << ")"
        << " | 识别 J_type=" << result.j_type << "(" << jtype_name << ")"
        << " | 耗时=" << result.elapsed << "s";
    result.log = oss.str();

    return result;
}


// ═══════════════════════════════════════════════════════════════════
//  2. 距离-多普勒处理 (Cases 1-5)
// ═══════════════════════════════════════════════════════════════════

ProcessingResultRD run_processing_rd(int case_num, const std::string& config_path) {
    ProcessingResultRD result;
    result.case_num = case_num;

    auto t_start = chrono::high_resolution_clock::now();

    // ── 1. 加载配置 ──
    if (!Config::instance().loadFromFile(config_path)) {
        result.log = "[ERROR] 无法加载配置文件: " + config_path;
        return result;
    }

    // ── 2. 初始化全局变量 ──
    init_globals();
    int _nrn = nrn();
    int _nan1 = nan1();

    // ── 3. 生成波形 (调用 Module01 的 libwaveform_core.a) ──
    WaveformParams wp = build_waveform_params();
    WaveformResult wf = generate_waveform(case_num, wp);

    // ── 4. 设置全局信号矩阵 ──
    Radar_Sig = wf.radar_sig;
    if (wf.has_freq_seq) {
        f = wf.freq_seq;        // Case 3/4 等效载频序列
    } else if (wf.has_f) {
        f = wf.f;               // Case 1/5 跳频序列
    }
    if (wf.has_phi1) {
        phi1 = wf.phi1;         // Case 2/5 随机相位序列
    }

    // ── 5. 执行信号处理 ──
    F = MatrixXcd::Zero(_nrn, _nan1);

    switch (case_num) {
        case 1: chuli_Case1(Radar_Sig, F); break;
        case 2: chuli_Case2(Radar_Sig, F); break;
        case 3: chuli_Case3(Radar_Sig, F); break;
        case 4: chuli_Case4(Radar_Sig, F); break;
        case 5: chuli_Case5(Radar_Sig, F); break;
        default:
            result.log = "[ERROR] 不支持的 Case: " + to_string(case_num) + " (仅支持 1-5)";
            return result;
    }

    // ── 6. 捕获结果 ──
    result.rd_map = F;
    result.input_signal = Radar_Sig;
    result.nrn = _nrn;
    result.nan1 = _nan1;

    // ── 7. 计算坐标轴 ──
    double _fs = fs(), _fc = fc(), _prt = prt(), _Rs = Rs(), _Vr = Vr();

    if (case_num == 1 || case_num == 5) {
        // xi = tnrn * c/2
        result.xi = tnrn * (c / 2.0);
        // dv with ambiguity resolution (Vr>0 = approaching)
        double v_max = c / (2.0 * _prt * _fc);
        int NUM = (int)floor((_Vr + v_max / 2.0) / v_max);
        result.dv.resize(_nan1);
        for (int j = 0; j < _nan1; ++j) {
            result.dv(j) = ((j - _nan1 / 2.0) * c / (2.0 * _prt * _fc * _nan1))
                         + NUM * (c / (2.0 * _prt * _fc));
        }
    } else if (case_num == 2) {
        // Case 2: xi = tnrn * c/2 (centered at Rs, matches fftshifted F)
        result.xi = tnrn * (c / 2.0);
        // Case 2: dv = centered velocity axis (matches fftshifted F)
        double _prf = 1.0 / _prt;
        result.dv = ((VectorXd::LinSpaced(_nan1, 0, _nan1 - 1).array() - _nan1 / 2.0)
                     * _prf * c / (2.0 * _fc * _nan1)).matrix();
    } else {
        // Cases 3, 4: xi = [-nrn/2:nrn/2-1]/fs * c/2 + Rs
        result.xi = ((VectorXd::LinSpaced(_nrn, 0, _nrn - 1).array() - _nrn / 2.0) / _fs) * (c / 2.0) + _Rs;
        // Approximate dv (without exact NUM — requires intermediate processing data)
        result.dv = ((VectorXd::LinSpaced(_nan1, 0, _nan1 - 1).array() - _nan1 / 2.0) * c
                     / (2.0 * _prt * _fc * _nan1));
    }

    // ── 8. 计时 + 日志 ──
    auto t_end = chrono::high_resolution_clock::now();
    result.elapsed = chrono::duration<double>(t_end - t_start).count();

    ostringstream oss;
    oss << fixed << setprecision(3);
    oss << "Case " << case_num
        << " | RD map: " << _nrn << "x" << _nan1
        << " | 峰值=" << 20.0 * log10(F.cwiseAbs().maxCoeff() + 1e-15) << " dB"
        << " | 耗时=" << result.elapsed << "s";
    result.log = oss.str();

    return result;
}


// ═══════════════════════════════════════════════════════════════════
//  3. 时频干扰解耦 (Case 6)
// ═══════════════════════════════════════════════════════════════════

ProcessingResultDecouple run_processing_decouple(int jam_type, const std::string& config_path) {
    ProcessingResultDecouple result;

    auto t_start = chrono::high_resolution_clock::now();

    // ── 1. 加载配置 ──
    if (!Config::instance().loadFromFile(config_path)) {
        result.log = "[ERROR] 无法加载配置文件: " + config_path;
        return result;
    }

    // ── 2. 初始化全局变量 ──
    init_globals();
    int _nrn = nrn();
    int _nan1 = nan1();

    // ── 3. 生成含干扰回波 ──
    EchoGeneratorResult4 echoResult = generateEcho4(_nan1, jam_type);
    // echo_signal: (cpiNum × nrn), 转置为 (nrn × nan1)
    Radar_Sig = echoResult.echo_signal.transpose();
    result.input_signal = Radar_Sig;
    result.nrn = _nrn;
    result.nan1 = _nan1;

    // ── 4. 逐脉冲执行 jamTarDivi (复刻 chuli_Case6 核心循环) ──
    MatrixXcd pulses_jam = MatrixXcd::Zero(_nrn, _nan1);
    MatrixXcd pulses_tgt = MatrixXcd::Zero(_nrn, _nan1);
    VectorXd gaojiepu_flags = VectorXd::Zero(_nan1);

    int gaojiepu_count = 0;
    double threshold_sum = 0;

    for (int i = 0; i < _nan1; ++i) {
        VectorXcd temp_data = Radar_Sig.col(i);
        JamTarDiviResult divi_result = jamTarDivi(temp_data);

        pulses_jam.col(i) = divi_result.jammingsignal;
        pulses_tgt.col(i) = divi_result.targetsignal;

        gaojiepu_flags(i) = divi_result.gaojiepu_idx;
        if (divi_result.gaojiepu_idx == 1) gaojiepu_count++;
        threshold_sum += divi_result.threshold;
    }

    result.jam_signal = pulses_jam;
    result.target_signal = pulses_tgt;
    result.decouple_flag = gaojiepu_flags;
    result.gaojiepu_count = gaojiepu_count;
    result.avg_threshold = threshold_sum / _nan1;

    // ── 5. 计算 JSR (复刻 03 模块公式)
    // JSR = 20*log10( (max|target|/max|jam|) / (max|s_echo_noise|/max|mixed|) )
    // 即: 分离后干信比 / 抑制前干信比 = 抑制改善量
    double sum_kkk = 0.0, sum_kkk1 = 0.0;
    int valid_count = 0;
    for (int i = 0; i < _nan1; ++i) {
        double max_mixed  = Radar_Sig.col(i).cwiseAbs().maxCoeff();
        double max_target = pulses_tgt.col(i).cwiseAbs().maxCoeff();
        double max_jam    = pulses_jam.col(i).cwiseAbs().maxCoeff();
        double max_noise_echo = echoResult.s_echo_noise.cwiseAbs().maxCoeff();

        double kkk  = (max_mixed > 1e-14) ? (max_noise_echo / max_mixed) : 0.0;
        double kkk1 = (max_jam > 1e-14)   ? (max_target / max_jam) : 0.0;

        if (kkk > 1e-14 && kkk1 > 1e-14) {
            sum_kkk  += kkk;
            sum_kkk1 += kkk1;
            valid_count++;
        }
    }
    if (valid_count > 0) {
        result.jsr_dB = 20.0 * log10((sum_kkk1 / valid_count) / (sum_kkk / valid_count));
    } else {
        result.jsr_dB = 0.0;
    }

    // ── 6. 计时 + 日志 ──
    auto t_end = chrono::high_resolution_clock::now();
    result.elapsed = chrono::duration<double>(t_end - t_start).count();

    const char* jam_names[] = {"", "ISDJ", "ISRJ", "ISCJ", "NBJ", "RDJ"};
    const char* jam_name = (jam_type >= 1 && jam_type <= 5) ? jam_names[jam_type] : "?";

    ostringstream oss;
    oss << fixed << setprecision(3);
    oss << "Case6 解耦 | 干扰=" << jam_type << "(" << jam_name << ")"
        << " | JSR干扰抑制比=" << result.jsr_dB << " dB"
        << " | 高阶谱=" << result.gaojiepu_count << "/" << _nan1
        << " | 平均阈值=" << result.avg_threshold
        << " | 耗时=" << result.elapsed << "s";
    result.log = oss.str();

    return result;
}
