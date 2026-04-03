function jammingsignal = PGO_simulate(fc,B,fs,prf,Tp,nan,R0,R1)
%% fc载波 B信号带宽 fs采样频率 R0雷达距目标初始距离 
%% prf发射脉冲重复频率 Vr径向相对速度 Tp发射信号脉宽 
%% nan方位向采样采样点数  JSR干信比 SNR信噪比 R1雷达与干扰机初始距离
c = 3e8;                                                                   %电磁波传播速度
lamda = c/fc;                                                              %波长
Kr = B/Tp;                                                                 %距离向线性调频率
nrn = 2*floor((fs*Tp+R0)/2);                                               %距离向采样点数
nrn = 2048;
tnrn=R0/c+(0:nrn-1)/fs;                                                    %距离向快时间
tnan=(0:nan-1)/prf;                                                        %方位向慢时间
s_echo = zeros(nrn,nan);
Vr=0;                                                                      %径向相对速度
A_t=1;                                                                     %目标散射系数
%% 无干扰回波信号
for m = 1:nan    
    R_t = R0-Vr*m/prf;                                                     %实时距离    
    win=((tnrn-2*R_t/c)<Tp)&((tnrn-2*R_t/c)>0);                            %回波的距离窗   
    s_echo(:,m)=s_echo(:,m)+(A_t.*win.*exp(1j*pi*Kr*(tnrn-2*R_t/c).^2).*exp(1j*2*pi*fc*(-2*R_t/c))).';   
end
%% PGO JAMMING
deltaf0=-fs/4 + (fs/4 - -fs/4) * rand();
jammingsignal = zeros(nrn,nan);
for m = 1:nan    
    R_t = R1-Vr*m/prf;                                                     %实时距离    
    win=((tnrn-2*R_t/c)<Tp*3/4)&((tnrn-2*R_t/c)>Tp*1/4);                            %回波的距离窗   
    jammingsignal(:,m)=jammingsignal(:,m)+(A_t.*win.*exp(1j*pi*Kr*(tnrn-2*R_t/c).^2)...
        .*exp(1j*2*pi*fc*(tnrn-2*R_t/c))).'.*exp(1j*2*deltaf0*(tnrn-2*R_t/c)).';   
end
jammingsignal=jammingsignal.';
end
