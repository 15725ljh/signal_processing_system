function jammingsignal = RDJ_simulate(fc,B,fs,prf,Tp,nan,R0,R1)
%% fcز BźŴ fsƵ R0״Ŀʼ 
%% prfظƵ Vrٶ Tpź 
%% nanλ  JSRű SNR R1״Żʼ
c = 3e8;                                                                   %Ųٶ
lamda = c/fc;                                                              %
Kr = B/Tp;                                                                 %ԵƵ
nrn = 2*floor((fs*Tp+R0)/2);                                               %
nrn = 2048;
tnrn=R0/c+(0:nrn-1)/fs;                                                    %ʱ
tnan=(0:nan-1)/prf;                                                        %λʱ
s_echo = zeros(nrn,nan);
Vr=0;                                                                      %ٶ
A_t=1;                                                                     %Ŀɢϵ
%% ޸Żزź
for m = 1:nan    
    R_t = R0-Vr*m/prf;                                                     %ʵʱ    
    win=((tnrn-2*R_t/c)<Tp)&((tnrn-2*2*R_t/c)>0);                            %زľ봰   
    s_echo(:,m)=s_echo(:,m)+(A_t.*win.*exp(1j*pi*Kr*(tnrn-2*R_t/c).^2).*exp(1j*2*pi*fc*(-2*R_t/c))).';   
end
%% RDJ JAMMING
deltaf0=-fs/4 + (fs/4 - -fs/4) * rand();
jammingsignal = zeros(nrn,nan);
for m = 1:nan    
    R_t = R1-Vr*m/prf;                                                     %ʵʱ    
    win=((tnrn-2*R_t/c)<Tp*3/4)&((tnrn-2*R_t/c)>Tp*1/4);                            %زľ봰   
    jammingsignal(:,m)=jammingsignal(:,m)+(A_t.*win.*exp(1j*pi*Kr*(tnrn-2*R_t/c).^2)...
        .*exp(1j*2*pi*fc*(tnrn-2*R_t/c))).'.*exp(1j*2*deltaf0*(tnrn-2*R_t/c)).';   
end
jammingsignal=jammingsignal.';
end