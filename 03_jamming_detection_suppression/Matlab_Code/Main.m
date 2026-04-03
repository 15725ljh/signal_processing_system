clear;close all;clc;
Fs=120e6;   % 采样率
B=80e6;     % 带宽
Tp=12e-6;   % 脉宽
CpiNum = 5;     % 原始为 500  处理脉冲较多，时间较长
  
%干扰类型选择
%['0无干扰",'1间歇直接转发干扰ISDJ","2间歇重复转发干扰ISRJ","3间歇循环转发干扰ISCJ","4窄带瞄频干扰NBJ","5距离欺骗干扰RDJ']

RealLabel = 5; 
%% 干扰信号生成
% 输入
% 脉冲数与干扰类型    
% 输出 
% s_echo 每个脉冲的回波    s_echo_noise 每个脉冲的含噪声回波
% Echo(干扰+底噪+回波) 维度： [CpiNum nrn]  complex double 
[s_echo,s_echo_noise,Echo] = EchoGener(CpiNum,RealLabel); 
% stft(s_echo);stft(s_echo_noise);stft(Echo(1,:));
%% 时频图对比
is_figure_on = 0;         % 此绘图开关关闭，不启用tfrstft   JamLocated   函数
if(is_figure_on==1)
    if CpiNum == 1
        STFT_NUM=256;
        tfr=tfrstft(Echo.',1:2048,STFT_NUM,hamming(31));
        figure(1),
        subplot(211),imagesc(fftshift(log(abs(tfr)+1),1)); title('STFT时频图','FontWeight','bold','FontSize',9)
        jam_tfr=JamLocated(tfr);
        subplot(212),imagesc(fftshift(jam_tfr,1)); title('干扰时频定位','FontWeight','bold','FontSize',9)
    end
    if CpiNum ~= 1
        STFT_NUM=256;
        tfr=tfrstft(Echo(1,:).',1:2048,STFT_NUM,hamming(31));
        figure(1),
        subplot(211),imagesc(fftshift(log(abs(tfr)+1),1)); title('STFT时频图','FontWeight','bold','FontSize',9)
        jam_tfr=JamLocated(tfr);
        subplot(212),imagesc(fftshift(jam_tfr,1)); title('干扰时频定位','FontWeight','bold','FontSize',9)
    end
end
%% 干扰识别
parfor num = 1:CpiNum
    J_type(num)=gr_detection(B,Tp,Echo(num,:));
end
%% 打印干扰识别结果
uniqueVectors = countDuplicateVectors(J_type, 1);

switch uniqueVectors(1)
    case 1
       sprintf('干扰类型为间歇采样转发干扰')
    case 2 
       sprintf('干扰类型为距离欺骗干扰')
    case 3
       sprintf('干扰类型为窄带瞄频干扰')
end
%% 新增：时频干扰抑制
[jammingsignal, Targetsignal, gaojiepu_idx] = JamTarDivi(Echo(1,:).');
is_figure_on1 = 1;
if(is_figure_on1==1)        % 这里转cpp不使用   不启用绘图开关 不使用stft
    figure(1111),subplot(311),stft(Echo(1,:).');title('干扰抑制前的总回波时频')
    figure(1111),subplot(312),stft(jammingsignal);title('干扰抑制后得到的干扰信号时频')
    figure(1111),subplot(313),stft(Targetsignal);title('干扰抑制后得到的纯净回波信号时频')
end
kkk =db(  max(abs(s_echo_noise)) / max(abs(Echo(1,:).'))  );
kkk1 =db(  max(abs(Targetsignal)) / max(abs(jammingsignal))  );
fprintf('干扰抑制比为 %.2f dB\n', kkk1 - kkk);
