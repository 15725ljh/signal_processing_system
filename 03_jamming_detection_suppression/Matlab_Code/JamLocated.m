function jam_tfr = JamLocated(tfr)
% JamLocated  基于 Otsu 方法从时频表示中检测干扰区域（Jamming Region）
%
% 功能：
%   给定一个复数时频分布矩阵 tfr，本函数通过以下步骤提取出可能由干扰信号
%   占据的区域，输出一个二值掩膜矩阵 jam_tfr，其中：
%       1 表示“干净”区域（非干扰）
%       0 表示“干扰”区域（被判定为 jamming）
%
%   该方法基于图像处理中的 Otsu 阈值分割技术，将时频图的幅度值视为灰度图像，
%   自动寻找最佳阈值来区分背景（低能量）和前景（高能量干扰）。
%
% 输入参数：
%   tfr     - 复矩阵，double 类型，大小为 [N x M]，表示时频分布（如 STFT 结果）。
%             通常由 tfrstft 等函数生成，每一列对应一个时间点的频谱。
%
% 输出参数：
%   jam_tfr - 逻辑/双精度矩阵，大小与 tfr 相同 [N x M]，为二值掩膜：
%               1：表示该时频点不属于干扰区域（保留）
%               0：表示该时频点属于干扰区域（需抑制或标记）
%             注意：此函数输出的是“保留区域”掩膜，0 表示 jamming。
%
% 算法流程：
%   1. 计算 |tfr| 幅度谱，并归一化到 [0,255] 范围，模拟 8 位灰度图像。
%   2. 统计各灰度级（0~255）的像素出现概率。
%   3. 使用 Otsu 算法遍历所有可能的分割阈值，最大化类间方差。
%   4. 找到最优阈值后，减去 10（经验性偏移），增强对弱干扰的敏感性。
%   5. 将低于该阈值的点设为干扰（置 0），其余为正常信号（置 1）。
%
% 应用场景：
%   - 雷达、通信系统中干扰信号检测与抑制
%   - 时频域去噪预处理
%   - 自动识别突发性宽带/窄带干扰
%
% 示例：
%   tfr = tfrstft(x, 1:length(x), 128, hamming(31));
%   jam_mask = JamLocated(tfr);
%   tfr_denoised = tfr .* jam_mask;  % 抑制干扰区域
%   imagesc(abs(tfr_denoised)); title('Denoised TFR');
%

% Step 1: 计算时频图的幅度绝对值
abs_TFR = abs(tfr);  % 大小仍为 [N x M]

% Step 2: 归一化幅度值至 [0, 255]，用于 8 位灰度量化
delta_P = (max(max(abs_TFR)) - min(min(abs_TFR))) / 255;  % 量化步长
TFR_RGB = round(abs_TFR ./ delta_P);  % 量化为 0~255 的整数灰度图

% 确保数值在合法范围内（防止 round 超出）
TFR_RGB = min(max(TFR_RGB, 0), 255);

% 获取总像素数
num = size(TFR_RGB, 1) * size(TFR_RGB, 2);

% Step 3: 统计每个灰度级（0~255）的出现概率
p = zeros(1, 256);  % p(k) 表示灰度值 k-1 的概率（索引从1开始）
for rgb = 1:256
    gray_level = rgb - 1;
    p(rgb) = sum(TFR_RGB(:) == gray_level) / num;  % 概率 = 频数 / 总数
end

% Step 4: 使用 Otsu 算法计算类间方差
%         目标：找到一个阈值，使前景和背景之间的类间方差最大
vara = zeros(1, 256);  % 存储每个阈值下的类间方差

for th = 1:256  % 遍历所有可能的阈值（对应灰度级 0~255）
    % 分成两类：
    %   C1: 灰度值 <= th-1 （背景）
    %   C2: 灰度值 >  th-1 （前景）
    p1 = sum(p(1:th));      % 背景总概率
    p2 = sum(p(th+1:end));  % 前景总概率
    
    % 避免除零
    if p1 == 0 || p2 == 0
        vara(th) = 0;
        continue;
    end
    
    % 计算两类的平均灰度值（加权均值）
    m1 = 0;  % 背景均值
    for j = 1:th
        m1 = m1 + (j - 1) * p(j);
    end
    m1 = m1 / p1;

    m2 = 0;  % 前景均值
    for j = th+1:256
        m2 = m2 + (j - 1) * p(j);
    end
    m2 = m2 / p2;

    % 计算类间方差
    vara(th) = p1 * p2 * (m1 - m2)^2;
end

% Step 5: 找到使类间方差最大的最佳阈值
[~, idx] = max(vara);
index = idx - 10;  % 关键：人为降低阈值 10 个等级，以更敏感地检测弱干扰
                   % 这是一个经验性调整，可能根据应用场景修改

% 保证 index 不越界
index = max(min(index, 255), 0);

% Step 6: 构造二值掩膜 jam_tfr
%         若量化后的灰度值 < index，则认为是干扰区域，置 0
%         否则保留为 1
jam_tfr = ones(size(tfr));  % 初始化为全1（全部保留）

% 逐点判断
for m = 1:size(tfr, 1)
    for n = 1:size(tfr, 2)
        if TFR_RGB(m, n) < index
            jam_tfr(m, n) = 0;
        end
    end
end

end