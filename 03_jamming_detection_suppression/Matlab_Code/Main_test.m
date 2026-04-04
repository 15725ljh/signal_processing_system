clear; close all; clc;
Fs=120e6;
B=80e6;
Tp=12e-6;
CpiNum = 100;
typeNames = {'无干扰', 'ISDJ/ISRJ/ISCJ', 'RDJ', 'NBJ'};
expectedType = [0, 1, 1, 1, 3, 2];

logFile = fopen('../output/matlab_output_log.txt', 'w');
t_total_start = tic;

fprintf(logFile, '==================== 干扰识别与抑制测试 ====================\n');
fprintf(logFile, '参数: B=80MHz, Tp=12us, CpiNum=%d\n\n', CpiNum);

results = zeros(5, 5);

for RealLabel = 1:5
    t_start = tic;

    [s_echo, s_echo_noise, Echo] = EchoGener(CpiNum, RealLabel);

    J_type = zeros(1, CpiNum);
    for num = 1:CpiNum
        J_type(num) = gr_detection(B, Tp, Echo(num, :));
    end

    uniqueVectors = countDuplicateVectors(J_type, 1);
    dominant_type = uniqueVectors(1);
    correct_count = sum(J_type == dominant_type);

    [jammingsignal, Targetsignal, gaojiepu_idx] = JamTarDivi(Echo(1, :).');

    kkk = 20*log10(max(abs(s_echo_noise)) / max(abs(Echo(1, :))));
    kkk1 = 20*log10(max(abs(Targetsignal)) / max(abs(jammingsignal)));
    JSR = kkk1 - kkk;

    filename = sprintf('../output/matlab_all_signals_type%d.mat', RealLabel);
    save(filename, 's_echo_noise', 'Echo', 'jammingsignal', 'Targetsignal', '-v7.3');

    elapsed = toc(t_start);
    results(RealLabel, :) = [RealLabel, dominant_type, correct_count, JSR, elapsed];

    fprintf(logFile, 'RealLabel=%d | 识别=%d (%s) | 正确=%d/%d | JSR干扰抑制比=%.2f dB | 耗时=%.3fs | %s\n', ...
        RealLabel, dominant_type, typeNames{dominant_type+1}, correct_count, CpiNum, JSR, elapsed, filename);
end

total_time = toc(t_total_start);

fprintf(logFile, '\n==================== 汇总 ====================\n');
fprintf(logFile, 'RealLabel | 识别结果 | 类型名称       | 正确率   | JSR干扰抑制比(dB)  | 耗时(s)\n');
fprintf(logFile, '----------|----------|----------------|----------|----------|--------\n');
total_correct = 0;
for i = 1:5
    exp = expectedType(i+1);
    if results(i,2) == exp
        correct = results(i, 3);
    else
        correct = 0;
    end
    total_correct = total_correct + correct;
    fprintf(logFile, '    %d     |    %d     | %-14s | %3d/%-5d | %7.2f | %.3f\n', ...
        results(i,1), results(i,2), typeNames{results(i,2)+1}, correct, CpiNum, results(i,4), results(i,5));
end
fprintf(logFile, '----------|----------|----------------|----------|----------|--------\n');
fprintf(logFile, '总正确: %d/%d | 总耗时: %.2fs\n', total_correct, CpiNum*5, total_time);
fprintf(logFile, '==================================================\n');

fclose(logFile);
