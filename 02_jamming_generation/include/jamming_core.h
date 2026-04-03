#ifndef JAMMING_CORE_H
#define JAMMING_CORE_H

#include "jamming_params.h"
#include <Eigen/Dense>
#include <complex>
#include <string>

struct JammingResult {
    Eigen::MatrixXcd echo_target;   // 含干扰回波 (nrn x nan1)
    Eigen::MatrixXcd jam_signal;    // 纯干扰信号 (nrn x nan1)
    Eigen::MatrixXcd target_signal; // 纯目标回波 (nrn x nan1)
    int nrn  = 0;
    int nan1 = 0;
    std::string log;
};

// mode: 1~10, 返回含干扰回波 + 纯干扰 + 纯目标
JammingResult generate_jamming(int mode, const JammingParams& params);

#endif // JAMMING_CORE_H
