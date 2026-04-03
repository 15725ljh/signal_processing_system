function [jammingsignal] = ISDJ_simulate(fc,B,fs,prf,Tp,nan,R0,R1)
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
    s_echo(:,m)=s_echo(:,m)+(A_t.*win.*exp(1j*pi*Kr*(tnrn-2*R_t/c).^2).*exp(1j*2*pi*fc*(tnrn-2*R_t/c))).';    
end
%% ISDJ 间歇采样直接转发干扰 占空比为50%
s_ISDJ = zeros(nrn,nan);                                                   %ISDJ回波空间
R_ISDJ = R1;                                                               %雷达与干扰机初始距离
V_ISDJ = 0;                                                                %雷达与干扰机相对径向速度
Ts_ISDJ = 2e-6;                                                            %间歇采样时间周期
T_ISDJ = 1/2*Ts_ISDJ;                                                      %间歇采样脉宽,进入主瓣干扰内：T_ISDJ<=TP*1/sqrt(D)
N_ISDJ = fix(Tp/Ts_ISDJ);                                                  %切片个数
R_ahead_ISDJ =0;                                                           %前置距离范围 
deltaf0=-fs/6 + (fs/6 - -fs/6) * rand();
for m =1:nan   
    R_ISDJ = R_ISDJ-V_ISDJ*m/prf-R_ahead_ISDJ;   
    s_ISDJ1=zeros(1,nrn);
    for Nc=0:(N_ISDJ-1)
        win1 = ((tnrn-2*R_ISDJ/c-(2*Nc+1)*T_ISDJ)<T_ISDJ)...
              &((tnrn-2*R_ISDJ/c-(2*Nc+1)*T_ISDJ)>0);                      %采样的窗函数       
        winjj = ((tnrn-2*R_ISDJ/c-T_ISDJ)<Tp)&((tnrn-2*R_ISDJ/c-T_ISDJ)>0);%防止溢出一个脉冲        
        s_ISDJ1=s_ISDJ1+ win1.*winjj.*exp(1j*pi*Kr*(tnrn-2*R_ISDJ/c-T_ISDJ).^2)...
                .*exp(1j*2*pi*fc*(tnrn-2*R_ISDJ/c-T_ISDJ)).*exp(1j*2*deltaf0*(tnrn-2*R_ISDJ/c-T_ISDJ));
    end
    s_ISDJ(:,m) = s_ISDJ1;
end
jammingsignal = s_ISDJ;
jammingsignal = jammingsignal.';

end
