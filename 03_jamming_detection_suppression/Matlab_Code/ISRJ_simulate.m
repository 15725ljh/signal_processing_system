function jammingsignal = ISRJ_simulate(fc,B,fs,prf,Tp,nan,R0,R1)
%% fc载波 B信号带宽 fs采样频率 R0雷达距目标初始距离 
%% prf发射脉冲重复频率 Tp发射信号脉宽 
%% nan方位向采样采样点数 JSR干信比 SNR信噪比 R1雷达与干扰机初始距离
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
%% ISRJ 间歇采样重复转发干扰，即对采样后的回波转发多次，也是欠采样
s_ISRJ = zeros(nrn,nan);                                                   %ISRJ回波空间
R_ISRJ = R1;                                                               %雷达与干扰机初始距离
V_ISRJ = 0;                                                                %雷达与干扰机相对径向速度
Ts_ISRJ = 4e-6;                                                            %间歇采样时间周期
T_ISRJ = 1/4*Ts_ISRJ;                                                      %间歇采样脉宽,假目标群可分：T_ISRJ>=sqrt(2/Kr)
N_ISRJ = fix(Tp/Ts_ISRJ);                                                  %切片数
Num_ISRJ = 2;                                                              %转发次数?<=fix(Ts_ISRJ/T_ISRJ)-1
Num_C_I = fix(Ts_ISRJ/T_ISRJ)-1;                                           %切片重组干扰
R_ahead_ISRJ =0;                                                           % 前置距离范围 （c/2(T_ISRJ-1/KrT_ISDJ),c/2(Num_C_I*T_ISRJ+1/KrT_ISDJ)）
deltaf0=-fs/6 + (fs/6 - -fs/6) * rand();
for m =1:nan   
    R_ISRJ = R_ISRJ-V_ISRJ*m/prf-R_ahead_ISRJ;
    s_ISRJ2=zeros(1,nrn);    
    for  Ni = 1:Num_C_I                                                    %转发次数        
        s_ISRJ1=zeros(1,nrn);       
        for Nc=0:N_ISRJ-1                                                  %切片个数                       
            win1 = ((tnrn-2*R_ISRJ/c-Nc*Ts_ISRJ-Ni*T_ISRJ)<T_ISRJ)...
                   &((tnrn-2*R_ISRJ/c-Nc*Ts_ISRJ-Ni*T_ISRJ)>0);            %采样的窗函数            
            winjj = ((tnrn-2*R_ISRJ/c-Ni*T_ISRJ)<Tp)...
                   &((tnrn-2*R_ISRJ/c-Ni*T_ISRJ)>0);                       %防止溢出转发超出波门外的值
            s_ISRJ1=s_ISRJ1+ win1.*winjj.*exp(1j*pi*Kr*(tnrn-2*R_ISRJ/c-Ni*T_ISRJ).^2)...
                    .*exp(1j*2*pi*fc*(tnrn-2*R_ISRJ/c-Ni*T_ISRJ)).*exp(1j*2*deltaf0*(tnrn-2*R_ISRJ/c-T_ISRJ));
        end
        s_ISRJ2=s_ISRJ2+s_ISRJ1;     
    end
    s_ISRJ(:,m) = s_ISRJ2;
end
jammingsignal = s_ISRJ.';
end