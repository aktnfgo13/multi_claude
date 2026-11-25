"""DI noise / gain-staging analysis used for the AB763 M2 hiss report (v0.6.1).
Usage:
  python3 tools/benchmark/di_noise_analysis.py levels DI.wav OUT1.wav [OUT2.wav ...]   # peak, playing/quiet RMS, level-matched quiet bands
  python3 tools/benchmark/di_noise_analysis.py tf PROBE_IN.wav PROBE_OUT.wav LATENCY LABEL  # small-signal transfer re 1 kHz
Quiet/playing windows are set for 026.wav (11.3-12.4 s / 2.0-9.5 s); change QUIET/PLAY for other files.
Outputs rendered by apps/mfx_render are compensated by the 2x oversampler latency (31 samples) when the name contains "amp".
"""
import numpy as np, struct, sys
def rd(p):
    d=open(p,'rb').read(); i=12
    while i<len(d):
        cid=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
        if cid==b'fmt ': fmt=struct.unpack('<HHIIHH',d[i+8:i+24])
        if cid==b'data': data=d[i+8:i+8+sz]; break
        i+=8+sz+(sz&1)
    tag,ch,fs,_,_,bits=fmt
    x=np.frombuffer(data,'<f4') if tag==3 else np.frombuffer(data,'<i2')/32768
    return x.reshape(-1,ch).mean(1).astype(np.float64), fs
def rms(x): return 20*np.log10(np.sqrt((x**2).mean())+1e-20)
def bands(x,fs):
    N=4096; win=np.hanning(N); acc=0;c=0
    for k in range(0,len(x)-N,N//2): acc=acc+abs(np.fft.rfft(x[k:k+N]*win))**2; c+=1
    p=acc/c; p=p/p.sum()*np.mean(x**2)  # normalise to mean square
    f=np.fft.rfftfreq(N,1/fs); out=[]
    for a,b in [(20,100),(100,300),(300,1000),(1000,3000),(3000,6000),(6000,10000),(10000,20000)]:
        m=(f>=a)&(f<b); out.append(10*np.log10(p[m].sum()+1e-30))
    return out
def levels(argv):
    lat=31
    ref,fs=rd(argv[0])
    play=(int(2.0*fs),int(9.5*fs)); quiet=(int(11.3*fs),int(12.4*fs)); hold=None
    print('%-16s %8s %8s %8s %8s %8s'%('file','peak','rmsPlay','rmsQuiet','SNRplay/q','gainMatch'))
    res={}
    for p in argv:
        x,_=rd(p); x=x[lat:] if 'amp' in p else x
        pk=20*np.log10(abs(x).max()); rp=rms(x[play[0]:play[1]]); rq=rms(x[quiet[0]:quiet[1]])
        res[p]=(x,rp)
        print('%-16s %8.1f %8.1f %8.1f %8.1f'%(p.split('/')[-1],pk,rp,rq,rp-rq))
    print('quiet-section bands (dBFS, level-matched to playing RMS of first file): 20-100,100-300,300-1k,1-3k,3-6k,6-10k,10-20k')
    r0=res[argv[0]][1]
    for p in argv:
        x,rp=res[p]; g=10**((r0-rp)/20)
        print('%-16s'%p.split('/')[-1],' '.join('%6.1f'%v for v in bands(x[quiet[0]:quiet[1]]*g,fs)), ' | play:',' '.join('%6.1f'%v for v in bands(x[play[0]:play[1]]*g,fs)))

fr=[95,1000,4000,7000,10000,14000,18000,20000]
def tf(xin,yout,lat):
    x,fs=rd(xin); y,_=rd(yout); y=y[lat:lat+len(x)]
    N=8192; w=np.hanning(N); Sxy=0;Sxx=0
    for k in range(fs,len(x)-N,N//2):
        X=np.fft.rfft(x[k:k+N]*w); Y=np.fft.rfft(y[k:k+N]*w); Sxy=Sxy+np.conj(X)*Y; Sxx=Sxx+abs(X)**2
    h=abs(Sxy/Sxx); f=np.fft.rfftfreq(N,1/fs)
    v=[20*np.log10(h[(f>q/1.06)&(f<q*1.06)].mean()) for q in fr]
    return v, np.isfinite(y).all(), abs(y).max()

if __name__ == '__main__':
    if sys.argv[1] == 'levels':
        levels(sys.argv[2:])
    else:
        v, ok, pk = tf(sys.argv[2], sys.argv[3], int(sys.argv[4]))
        print(f'{sys.argv[5]:<34}' + ' '.join(f'{a - v[1]:6.1f}' for a in v) + f'  | 1k {v[1]:6.1f} dB finite {ok} peak {pk:.2e}')
