function [jammingsignal, Targetsignal, gaojiepu_idx] = JamTarDivi(data)
    % JamTarDivi - 将输入信号分解为干扰信号和目标信号
    % [jammingsignal, Targetsignal, jam_tfr, target_tfr, gaojiepu_idx] = JamTarDivi(data)
    %
    % 输入:
    %   data - 待处理的输入信号（列向量，通常为复数信号）
    %
    % 输出:
    %   jammingsignal - 提取的干扰信号（时域）
    %   Targetsignal - 提取的目标信号（时域）
    %   jam_tfr - 干扰信号的短时傅里叶变换（STFT）表示
    %   target_tfr - 目标信号的STFT表示
    %   gaojiepu_idx - 高阶谱标志（用于判断目标信号是否为高阶谱，当前为简单实现）
    %
    % 功能概述:
    %   本函数通过短时傅里叶变换（STFT）对输入信号进行时频分析，
    %   利用Tsallis交叉熵分割时频图，分离出干扰信号和目标信号。
    
    % 初始化高阶谱标志（当前仅为占位符，值为复数零）
    % gaojiepu_idx = complex(zeros(1,1));
    
    % 设置STFT参数：频域分量数为256
    STFT_NUM = 256;
    % 计算输入信号的STFT，使用汉明窗（长度31）
    tfr = tfrstft(data, 1:length(data), STFT_NUM, hamming(31));
    % tfr - STFT结果（复数矩阵，行数为频率点数，列数为时间点数）
    
    % 获取tfr的维度
    [N, M] = size(tfr);  % N - 频率点数, M - 时间点数
    
    % 计算tfr在频率轴上的和（沿时间轴求和）
    tfr_sum = sum(tfr);
    
    % 计算权重，用于后续信号重构（归一化因子）
    weight = mean(abs(tfr_sum)) / mean(abs(data));
    
    % 取STFT的幅度谱
    abs_TFR = abs(tfr);
    
    % 将幅度谱归一化并量化为0-255的灰度级
    delta_P = (max(max(abs_TFR)) - min(min(abs_TFR))) / 255;  % 量化步长
    TFR_RGB = round(abs_TFR ./ delta_P);  % 量化后的灰度值矩阵
    
    % 计算总像素数（时频图的总点数）
    num = N * M;
    
    % 初始化灰度直方图概率数组
    p = zeros(1, 256);
    % 计算每个灰度级的概率
    for rgb = 1:256
        p(rgb) = length(find(TFR_RGB == rgb-1)) / num;  % 灰度值从0到255
    end
    
    % 设置Tsallis熵参数
    q = 2;  % Tsallis熵的非广延性参数，影响分割效果         原值是6  越小对弱目标回波的提取越好，但不小于1
    
    % 初始化Tsallis交叉熵数组（支持复数计算，尽管当前为实数）
    D = zeros(1, 256);
    D = complex(D);
    
    % 遍历所有灰度级，计算Tsallis交叉熵
    for rgb = 1:256
        % 初始化变量
        m0 = 0;  % C0（灰度0到rgb-1）的平均灰度
        m1 = 0;  % C1（灰度rgb到255）的平均灰度
        D0 = 0;  % C0的Tsallis熵分量
        D1 = 0;  % C1的Tsallis熵分量
        
        % 计算C0和C1的概率和
        W0 = sum(p(1:rgb));      % C0的概率
        W1 = sum(p(rgb+1:end));  % C1的概率
        
        % 计算C0的平均灰度m0
        for i = 1:rgb
            m0 = m0 + (i-1) * p(i) / W0;
        end
        % 计算C1的平均灰度m1
        for j = rgb+1:256
            m1 = m1 + (j-1) * p(j) / W1;
        end
        
        % 计算C0的Tsallis交叉熵分量D0
        for i = 1:rgb
            if m0 ~= 0  % 避免除以零
                D0 = D0 + p(i) * (m0 * (i / m0)^q - i);
            end
        end
        % 计算C1的Tsallis交叉熵分量D1
        for j = rgb+1:256
            if m1 ~= 0  % 避免除以零
                D1 = D1 + p(j) * (m1 * (j / m1)^q - j);
            end
        end
        % 计算总的Tsallis交叉熵
        D(rgb) = D0 + D1 + D0 * D1;
    end
    
    % 找到使Tsallis交叉熵最小的灰度级（分割阈值）
    index = find(D == min(D));
    
    % 初始化干扰信号的时频掩模（全1矩阵）
    jam_tfr = ones(N, M);
    % 根据阈值生成掩模：灰度小于index的点置为0（目标区域）
    for m = 1:N
        for n = 1:M
            if TFR_RGB(m, n) < index
                jam_tfr(m, n) = 0;
            end
        end
    end
    
    % 初始化干扰信号的STFT表示
    JammingTFR = zeros(N, M);
    JammingTFR = complex(JammingTFR);  % 支持复数
    % 提取干扰信号的STFT（掩模为1的区域保留原始tfr值）
    JammingTFR(jam_tfr == 1) = tfr(jam_tfr == 1);
    
    % 重构干扰信号（时域）：对STFT沿频率轴求和并归一化
    jammingsignal = (sum(JammingTFR) / weight).';
    
    % 计算目标信号（时域）：原始信号减去干扰信号
    Targetsignal = data - jammingsignal;

    % 计算干扰信号的STFT
    % STFT_NUM = 256;
    % jam_tfr = tfrstft(jammingsignal, 1:length(data), STFT_NUM, hamming(31));
    % 计算目标信号的STFT
    % target_tfr = tfrstft(Targetsignal, 1:length(data), STFT_NUM, hamming(31));
   
    % 判断目标信号是否为高阶谱（简单实现，可能需优化）
    if abs(Targetsignal) < 35
        gaojiepu_idx = 1;  % 幅度小于35，标记为高阶谱
    else
        gaojiepu_idx = 0;  % 否则标记为非高阶谱
    end
end