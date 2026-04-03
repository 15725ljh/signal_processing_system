function [s_echo,s_echo_noise,Echo] = EchoGener(CpiNum,RealLabel)

%% �״����
fc = 35e9;                                                                 %�ز�
B = 80e6;                                                                  %�źŴ���
fs = 120e6;                                                                %����Ƶ��
R0 = 1e3;                                                                  %�״��Ŀ���ʼ����
prf = 5e3;                                                                 %���������ظ�Ƶ��
c = 3e8;                                                                   %��Ų������ٶ�
lamda = c/fc;                                                              %����
Vr = 0;                                                                    %��������ٶ�
Tp= 12e-6;                                                                 %�����ź�����
Kr = B/Tp;                                                                 %���������Ե�Ƶ��
nrn = 2*floor((fs*Tp+R0)/2);   nrn = 2048;                                 %�������������
tnrn = R0/c+(0:nrn-1)/fs;                                                  %�������ʱ��
CPI_num = CpiNum;                                                          %CPI����PRT��
nan = CPI_num;                                                             %��λ�������������
tnan = (0:nan-1)/prf;                                                      %��λ����ʱ��
A_t = 1;                                                                   %Ŀ��ɢ��ϵ��
SNR = 25;                                                                  %�����
%% �����޸��Żز�
s_echo=zeros(1,nrn);
R_t = R0-Vr*1/prf;                                                         %ʵʱ����
win = ((tnrn-2*R_t/c)<Tp)&((tnrn-2*R_t/c)>0);                              %�ز��ľ��봰
s_echo(1,1:nrn) = s_echo(1,1:nrn)+(A_t.*win.*exp(1j*pi*Kr*(tnrn-2*R_t/c).^2).*exp(1j*2*pi*fc*(tnrn-2*R_t/c)));
s_echo_noise = s_echo + randn(size(s_echo)) * sqrt(var(s_echo)/10^(SNR/10)) + 1j*randn(size(s_echo)) * sqrt(var(s_echo)/10^(SNR/10));
%coeff=conj(fliplr(s_echo));
%Npc_nrn=4096;
%coeff_fft=fft(coeff,Npc_nrn);
%% ����JSR
real_label=RealLabel;                                                      %��������
Echo_sig=zeros(CPI_num,nrn);                                               %����һ��CPI������
%JSR=15 + (25 - 15) * rand();

JSR = 30;

A=10.^(JSR/20);
for num = 1:CPI_num
    %% ��������
    %% ��Ъֱ��ת��
    if(real_label==1)
        dis_R=(1.7*R0-0.7*R0)/CPI_num;
        R1=0.7*R0:dis_R:1.7*R0-dis_R;
        jammingsignal=ISDJ_simulate(fc,B,fs,prf,Tp,1,R0,R1(round(1 + (CPI_num - 1) * rand())));
        jammingsignal=max(abs(s_echo_noise)).*A.*jammingsignal;
        sig=jammingsignal+s_echo_noise;
        % sig = awgn(sig,-3,'measured');  
    end
    %% ��Ъ�ظ�ת��
    if(real_label==2)
        dis_R=(1.7*R0-0.7*R0)/CPI_num;
        R1=0.7*R0:dis_R:1.7*R0-dis_R;
        jammingsignal=ISRJ_simulate(fc,B,fs,prf,Tp,1,R0,R1(round(1 + (CPI_num - 1) * rand())));
        jammingsignal=max(abs(s_echo_noise)).*A.*jammingsignal;
        sig=jammingsignal+s_echo_noise;
        % sig = awgn(sig,11,'measured');  
    end
    %% ��Ъѭ��ת��
    if(real_label==3)
        dis_R=(1.7*R0-0.7*R0)/CPI_num;
        R1=0.7*R0:dis_R:1.7*R0-dis_R;
        jammingsignal=ISCJ_simulate(fc,B,fs,prf,Tp,1,R0,R1(round(1 + (CPI_num - 1) * rand())));
        jammingsignal=max(abs(s_echo_noise)).*A.*jammingsignal;
        sig=jammingsignal+s_echo_noise;
        % sig = awgn(sig,2,'measured');
    end
    %% խ����Ƶ
    if(real_label==4)
        dis_R=(1.7*R0-0.7*R0)/CPI_num;
        R1=0.7*R0:dis_R:1.7*R0-dis_R;
        jammingsignal=Narrownoise_simulate(fc,B,fs,prf,Tp,1,R0,R1(round(1 + (CPI_num - 1) * rand())),B/8);
        jammingsignal=max(abs(s_echo_noise)).*A.*jammingsignal;
        sig=jammingsignal+s_echo_noise;
        % sig = awgn(sig,3.2,'measured');
    end
    %% ��������(������ƭ)
    if(real_label==5)
        dis_R=(1.7*R0-0.7*R0)/CPI_num;
        R1=0.7*R0:dis_R:1.7*R0-dis_R;
        jammingsignal=PGO_simulate(fc,B,fs,prf,Tp,1,R0,R1(round(1 + (CPI_num - 1) * rand())));
        jammingsignal=max(abs(s_echo_noise)).*A.*jammingsignal;
        sig=jammingsignal+s_echo_noise;
        % sig = awgn(sig,13,'measured');
    end



    sig = sig + randn(size(sig)) * sqrt(var(sig)/10^(25/10)) + 1j*randn(size(sig)) * sqrt(var(sig)/10^(25/10));      % �������ŵĴ���ز��ź������Ӹ�����
    Echo_sig(num,:)=sig;
%     h=waitbar(num/CpiNum); title('���ɽ���'); 
end
Echo=Echo_sig;
% close(h);

% plot(real(s_echo));           ����δ�������Ļز��ź�
% plot(real(s_echo_noise));     ���Ƽ������Ļز��ź�
% plot(real(Echo_sig(1,:)));    �����׸�����ĺ���׺͸��ŵĻز��ź�

end