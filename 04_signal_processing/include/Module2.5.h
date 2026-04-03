// 本模块包含了干扰识别函数(STFT时频分析 + 频谱特征提取 + 干扰类型判别)
#ifndef MODULE_shibie_H
#define MODULE_shibie_H
#include "Module0.h"          // 工具函数(STFT/JamLocated等) + 全局参数
#include "Config.h"           // 配置管理器(用于读取recognition.*参数)
#define CFG Config::instance()  // 配置管理器单例快捷宏

int shibie();                 // 干扰识别主函数:选取脉冲→调用gr_detection→打印识别结果

/*
 * 检测输入信号中的干扰类型
 * data 输入的复数向量数据,包含待分析的信号
 * 返回整数,表示检测到的干扰类型(0 表示未检测到特定类型,其他值对应不同干扰类型)
 */ 
inline int gr_detection(const VectorXcd& data) {
    // --- 初始化阶段 ---
    int J_type = 0;              // 干扰类型初始化为0,表示默认无特定干扰
    double T_Kr = B() / Tp();
    const int STFT_NUM = CFG.getInt("recognition.stft_num", 256);
    VectorXd h = generateHammingWindow(CFG.getInt("recognition.hamming_len", 63));
    VectorXd t_in = VectorXd::LinSpaced(nrn(), 1, nrn()); // 创建时间向量,从1到nrn(nrn为外部定义的信号长度)
    MatrixXcd tfr, jam_tfr;      // 定义复数矩阵：tfr存储STFT结果,jam_tfr存储干扰掩模矩阵,维度为 STFT_NUM * nrn
    VectorXd t_out, f;           // 定义STFT输出时间轴(t_out)和频率轴(f)

    // --- STFT分析阶段 ---
    // 使用短时傅里叶变换分析输入信号的时频特性
    tfrstft(data, t_in, STFT_NUM, h, false, tfr, t_out, f); // 计算data的STFT,结果存储在tfr中,t_out和f为输出时间和频率轴
    JamLocated(tfr, jam_tfr);  // 对STFT结果进行干扰定位处理,生成干扰掩模矩阵jam_tfr

    // --- 频谱调整 ---
    jam_tfr = fftshift(jam_tfr); // 将频谱零频移到中心,便于后续频域分析

    // --- 频域包络特征提取 ---
    VectorXd F_TEMP = jam_tfr.rowwise().sum().real(); // 沿时间轴求和,取实部,得到频域包络
    double miu1 = F_TEMP.mean() * CFG.getDouble("recognition.threshold_ratio", 0.3);
    VectorXd A1 = (F_TEMP.array() > miu1).cast<double>(); // 将频域包络二值化,大于阈值为1,否则为0

    // --- 上升沿和下降沿检测 ---
    vector<int> B1; // 存储频域包络上升沿的位置
    for (int j = 0; j < A1.size() - 1; ++j) {
        if (A1(j) == 0 && A1(j + 1) != 0) { // 检测从0到非0的跳变,表示上升沿
            B1.push_back(j);
        }
    }
    vector<int> B2; // 存储频域包络下降沿的位置
    for (int j = 0; j < A1.size() - 1; ++j) {
        if (A1(j) != 0 && A1(j + 1) == 0) { // 检测从非0到0的跳变,表示下降沿
            B2.push_back(j);
        }
    }
    if (B1.empty()) {
        B1.push_back(0); // 如果没有检测到上升沿,默认设置为索引0(C++索引从0开始)
    }

    // --- 干扰类型判别逻辑 ---
    if (B1.size() > 1) {
        // 情况1：多个上升沿,可能为梳状谱类干扰
        VectorXd T_TEMP = jam_tfr.colwise().sum().real().transpose(); // 沿频率轴求和,取实部,得到时域特征
        vector<int> h; // 存储时域特征的下降沿位置
        for (int j = 0; j < T_TEMP.size() - 1; ++j) {
            if (T_TEMP(j) != 0 && T_TEMP(j + 1) == 0) { // 检测时域特征的下降沿
                h.push_back(j);
            }
        }
        if (h.size() <= 1) {
            J_type = 9; // 时域下降沿少于等于1,判断为单间歇梳状谱干扰
        } else {
            // 进一步分析第一个子带的特性
            int freq_idx = min(B1[0] + 2, STFT_NUM - 1); // 选择第一个上升沿的上边界频率索引(偏移2个点)
            VectorXd H1 = jam_tfr.row(freq_idx).real(); // 提取该频率的时域数据
            vector<int> h_starts; // 存储该子带脉冲的起始位置(上升沿)
            for (int j = 0; j < H1.size() - 1; ++j) {
                if (H1(j) == 0 && H1(j + 1) != 0) {
                    h_starts.push_back(j);
                }
            }
            if (h_starts.size() == 1) {
                J_type = 1; // 只有一个脉冲起始,判断为常规梳状谱
            } else {
                // 统计每个子带的脉冲数
                vector<int> sub_pulse_counts(B1.size(), 0); // 初始化脉冲计数向量
                for (size_t j = 0; j < B1.size(); ++j) {
                    int freq_idx = min(B1[j] + 2, STFT_NUM - 1); // 每个子带的上边界频率索引
                    VectorXd current_band = jam_tfr.row(freq_idx).real(); // 提取该子带的时域数据
                    vector<int> valid_starts; // 存储当前子带的脉冲起始位置
                    for (int k = 0; k < current_band.size() - 1; ++k) {
                        if (current_band(k) == 0 && current_band(k + 1) != 0) {
                            valid_starts.push_back(k);
                        }
                    }
                    sub_pulse_counts[j] = valid_starts.size(); // 记录当前子带的脉冲数
                }
                // 计算脉冲数的统计特征
                double mean_counts = 0;     // 脉冲数均值
                for (int count : sub_pulse_counts) mean_counts += count;
                mean_counts /= sub_pulse_counts.size();
                double std_dev = 0;         // 脉冲数标准差
                for (int count : sub_pulse_counts) std_dev += pow(count - mean_counts, 2);
                std_dev = sqrt(std_dev / sub_pulse_counts.size());
                if (std_dev == 0) {
                    J_type = 2; // 标准差为0,脉冲数均匀,判断为规则梳状谱
                } else {
                    J_type = 3; // 标准差非0,脉冲数随机,判断为随机梳状谱
                }
            }
        }
    } else if (B1.size() == 1) {
        // 情况2：单个上升沿,可能为其他类型干扰
        int index1 = min(B1[0] + 5, STFT_NUM - 1); // 上边界频率索引(上升沿偏移5个点)
        int index2 = max(B2.empty() ? 0 : B2[0] - 2, 0); // 下边界频率索引(下降沿偏移2个点,或默认0)
        VectorXd C1 = jam_tfr.row(index1).real(); // 上边界频带的时域数据
        VectorXd C2 = jam_tfr.row(index2).real(); // 下边界频带的时域数据

        // 检测边界频带的上升沿和下降沿
        vector<int> D1; // 上边界频带的上升沿位置
        for (int j = 0; j < C1.size() - 1; ++j) {
            if (C1(j) == 0 && C1(j + 1) != 0) {
                D1.push_back(j);
            }
        }
        vector<int> D2; // 下边界频带的下降沿位置
        for (int j = 0; j < C2.size() - 1; ++j) {
            if (C2(j) != 0 && C2(j + 1) == 0) {
                D2.push_back(j);
            }
        }

        if (D1.size() == 1) {
            // 上边界只有一个上升沿,分析信号幅度特性
            VectorXd data_ABS = data.array().abs(); // 计算输入信号的幅度
            double threshold = (data_ABS.maxCoeff() + data_ABS.minCoeff()) / 2; // 阈值为幅度最大值和最小值的平均
            VectorXd E1 = (data_ABS.array() > threshold).cast<double>(); // 二值化幅度,大于阈值为1
            VectorXd E2 = E1 * data_ABS.mean(); // 二值化结果乘以幅度均值
            vector<int> E3; // 存储E2的上升沿位置
            for (int j = 0; j < E2.size() - 1; ++j) {
                if (E2(j) == 0 && E2(j + 1) != 0) {
                    E3.push_back(j);
                }
            }
            if (E3.size() > 1) {
                J_type = 9; // 多个上升沿,判断为梳状谱干扰
            } else {
                J_type = 10; // 单一上升沿,判断为联合拖引干扰
            }
        } else {
            // 上边界有多个上升沿,分析周期性
            vector<int> J_Ts1; // 上边界上升沿的时间间隔
            if (D1.size() > 1) {
                for (size_t j = 0; j < D1.size() - 1; ++j) {
                    J_Ts1.push_back(D1[j + 1] - D1[j]);
                }
            }
            vector<int> J_Ts2; // 下边界下降沿的时间间隔
            if (D2.size() > 1) {
                for (size_t j = 0; j < D2.size() - 1; ++j) {
                    J_Ts2.push_back(D2[j + 1] - D2[j]);
                }
            }
            double mean_J_Ts1 = 0, mean_J_Ts2 = 0; // 时间间隔均值
            if (!J_Ts1.empty()) {
                for (int ts : J_Ts1) mean_J_Ts1 += ts;
                mean_J_Ts1 /= J_Ts1.size();
            }
            if (!J_Ts2.empty()) {
                for (int ts : J_Ts2) mean_J_Ts2 += ts;
                mean_J_Ts2 /= J_Ts2.size();
            }
            double std_J_Ts1 = 0, std_J_Ts2 = 0; // 时间间隔标准差
            for (int ts : J_Ts1) std_J_Ts1 += abs(ts - mean_J_Ts1);
            for (int ts : J_Ts2) std_J_Ts2 += abs(ts - mean_J_Ts2);
            if (!J_Ts1.empty()) std_J_Ts1 /= J_Ts1.size();
            if (!J_Ts2.empty()) std_J_Ts2 /= J_Ts2.size();

            if (std_J_Ts1 < CFG.getDouble("recognition.periodic_std1", 25) && std_J_Ts2 < CFG.getDouble("recognition.periodic_std2", 6)) {
                // 时间间隔标准差较小,具有周期性,计算调频率
                VectorXd T_TEMP = jam_tfr.rowwise().sum().real(); // 频域包络
                int T_activity = -1; // 第一个非零元素位置
                for (int j = 0; j < T_TEMP.size(); ++j) {
                    if (T_TEMP(j) != 0) {
                        T_activity = j;
                        break;
                    }
                }
                if (T_activity == -1) {
                    return 0; // 无活动信号,返回0
                }
                int valid_pulses = min(D1.size(), D2.size()); // 有效脉冲数
                vector<double> J_Kr; // 存储每个脉冲的调频率
                for (int j = 0; j < valid_pulses; ++j) {
                    double delta_f = (index1 - index2) / double(STFT_NUM) * fs(); // 频率差,fs为采样率(外部定义)
                    double delta_t = (D2[j] - D1[j]) / fs(); // 时间差
                    if (delta_t != 0) {
                        J_Kr.push_back(delta_f / delta_t); // 计算调频率
                    }
                }
                if (J_Kr.empty()) {
                    return 0; // 无有效调频率,返回0
                }
                double mean_Kr = 0; // 平均调频率
                for (double kr : J_Kr) mean_Kr += kr;
                mean_Kr /= J_Kr.size();
                if (std::round(mean_Kr / T_Kr) == 1) {
                    // 平均调频率接近目标调频率
                    if (D1.size() < CFG.getInt("recognition.pulse_count_threshold", 10)) {
                        J_type = 7; // 脉冲数少于10,判断为密集假目标
                    } else {
                        J_type = 4; // 脉冲数较多,判断为频谱弥散
                    }
                } else {
                    J_type = 8; // 调频率不匹配,判断为扫频干扰
                }
            } else {
                // 时间间隔标准差较大,无明显周期性,分析带宽比例
                int freq_span_idx = max(B1[0], B2.empty() ? 0 : B2[0]) - min(B1[0], B2.empty() ? 0 : B2[0]); // 频率跨度索引
                double freq_span = freq_span_idx / double(STFT_NUM) * fs(); // 频率跨度
                double BW_ratio = B() / freq_span; // 带宽比例
                if (BW_ratio < CFG.getDouble("recognition.bw_ratio_threshold", 1.5)) {
                    J_type = 5; // 带宽比例小于1.5,判断为灵巧噪声干扰
                } else {
                    J_type = 6; // 带宽比例较大,判断为窄带干扰
                }
            }
        }
    }

    return J_type; // 返回检测到的干扰类型
}






#endif // MODULE_shibie_H
