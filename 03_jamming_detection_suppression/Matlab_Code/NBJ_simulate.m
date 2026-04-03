function jammingsignal = NBJ_simulate(fc,B,fs,prf,Tp,nan,R0,R1,j_Br)
if nargin==8
    j_Br=1/4*B;
end
c = 3e8;
lamda = c/fc;
Kr = B/Tp;
nrn = 2048;
tnrn=R0/c+(0:nrn-1)/fs;
%% narrow band jamming
for m=1:nan
    z=(randn(nrn,1)+1j*randn(nrn,1))*sqrt(5);
    Z=fft(z);
    Wn=(j_Br/2)/(fs/2);
    freq=(0:nrn-1)'*(fs/nrn);
    freq(freq>fs/2)=freq(freq>fs/2)-fs;
    H=1./sqrt(1+(freq/(Wn*fs/2)).^(2*8));
    Z=Z.*H;
    lvbo_z=ifft(Z);
    lvbo_z=lvbo_z./max(abs(lvbo_z));
    j(:,m)=lvbo_z.';
end
deltaf0=-fs/6 + (fs/6 - (-fs/6)) * rand();
R_t = R0;
j=j.*exp(1j*2*pi*deltaf0*(tnrn-2*R_t/c)).';
jammingsignal=j.';
end
