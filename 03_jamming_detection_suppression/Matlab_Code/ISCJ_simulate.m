function jammingsignal = ISCJ_simulate(fc,B,fs,prf,Tp,nan,R0,R1)
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
%% ISCJ 间歇采样循环转发干扰，对所有的采样存储信号进行倒序转发,切片的周期变化
s_ISCJ = zeros(nrn,nan);                                                   %ISCJ回波空间
R_ISCJ = R1;                                                               %雷达与干扰机初始距离
V_ISCJ = 0;                                                                %雷达与干扰机相对径向速度
T_ISCJ = 1e-6;                                                             %间歇采样脉宽
N_ISCJ = fix((sqrt(9+8*(fix(Tp/T_ISCJ)))-3)/2);                            %切片个数，2*(fix(Tp/T_ISCJ))>=(N_ISCJ+3)*N_ISCJ
R_ahead_ISCJ = 0;                               % 前置距离范围 （c/2(T_ISCJ-1/KrT_ISDJ),c/2*T_ISCJ(N_ISCJ.^2+3*N_ISCJ-2)/2）
deltaf0=-fs/6 + (fs/6 - -fs/6) * rand();
for m =1:nan   
    R_ISCJ = R_ISCJ-V_ISCJ*m/prf-R_ahead_ISCJ;   
    s_ISCJ2=zeros(1,nrn);   
    for  Ni = 1:N_ISCJ                                                     %切片个数        
        s_ISCJ1=zeros(1,nrn);        
        for Nc=1:N_ISCJ-Ni+1                                               %转发次数                      
            b = (Ni*(Ni+1)+(Nc-1)*(Nc+2*Ni+2))/2;                          %总延时系数； Ni*(Ni+1)/2-1为切片延时系数          
            a = (Nc-1)*(Nc+2*Ni+2)/2+1;                                    %每个切片转发延时系数           
            win1 = ((tnrn-2*R_ISCJ/c-b*T_ISCJ)<T_ISCJ)&((tnrn-2*R_ISCJ/c-b*T_ISCJ)>0);           
            winjj = ((tnrn-2*R_ISCJ/c-a*T_ISCJ)<Tp)&((tnrn-2*R_ISCJ/c-a*T_ISCJ)>0);  %防止溢出转发超出波门外的值
                s_ISCJ1=s_ISCJ1+ win1.*winjj.*exp(1j*pi*Kr*(tnrn-2*R_ISCJ/c-(a)*T_ISCJ).^2)...
                    .*exp(1j*2*pi*fc*(tnrn-2*R_ISCJ/c-(a)*T_ISCJ)).*exp(1j*2*deltaf0*(tnrn-2*R_ISCJ/c-T_ISCJ));
        end
        s_ISCJ2=s_ISCJ2+s_ISCJ1;    
    end
    s_ISCJ(:,m) = s_ISCJ2;
end
jammingsignal=s_ISCJ.';
end