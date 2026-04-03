#ifndef WAVEFORM_CORE_H
#define WAVEFORM_CORE_H

#include "waveform_params.h"
#include <Eigen/Dense>
#include <complex>
#include <string>

struct WaveformResult {
    Eigen::MatrixXcd radar_sig;   // 信号矩阵 (nrn x nan1)
    Eigen::VectorXd  f;           // 载频序列 (nan1), Case1/5 填充
    Eigen::VectorXcd phi1;       // 随机相位序列 (nan1), Case2/5 填充
    Eigen::VectorXd  freq_seq;   // 等效载频序列 (nan1), Case3/4 填充
    int nrn  = 0;
    int nan1 = 0;
    bool has_f        = false;
    bool has_phi1     = false;
    bool has_freq_seq = false;
    std::string log;
};

// mode: 1~5
WaveformResult generate_waveform(int mode, const WaveformParams& params);

#endif // WAVEFORM_CORE_H
