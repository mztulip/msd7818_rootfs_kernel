#!/usr/bin/env python3
"""adc_spectrum.py CAPTURE.bin... : Welch spectra of dmdts captures (split at gaps > 1 us), peaks, PNG next to this script name."""
import numpy as np, sys, os
from scipy.signal import welch, find_peaks
def load(fn):
    d=open(fn,'rb').read(); rate=np.frombuffer(d[:4],'<u4')[0]
    r=np.frombuffer(d[8:],dtype=[('c','<u4'),('w','<u2'),('p','<u2')])
    c=r['c'].astype(np.int64); x=(r['w']&0xff).astype(float)-128
    dc=np.diff(c); br=np.where(dc>276)[0]; edges=np.r_[0,br+1,len(c)]
    segs=[(a,b) for a,b in zip(edges[:-1],edges[1:]) if b-a>=8192]
    fs=rate/np.median(np.r_[[(c[b-1]-c[a])/(b-1-a) for a,b in segs]])
    return [x[a:b]-x[a:b].mean() for a,b in segs], fs
res={}
for fn in sys.argv[1:]:
    xs,fs=load(fn); x=np.concatenate(xs)
    ps=[welch(v,fs=fs,nperseg=4096)[1]*len(v) for v in xs]; f=welch(xs[0],fs=fs,nperseg=4096)[0]; p=np.sum(ps,0)/len(x)
    db=10*np.log10(p); floor=np.median(db)
    pk,_=find_peaks(db,height=floor+12,distance=20)
    top=sorted(pk,key=lambda k:-db[k])[:8]
    print('%s n=%d fs=%.3f MS/s floor %.1f dB'%(fn.split('/')[-1],len(x),fs/1e6,floor))
    print('   peaks (MHz, dB above floor):',', '.join('%.3f/%.0f'%(f[k]/1e6,db[k]-floor) for k in sorted(top)))
    res[fn]=(f,db)
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
fig,ax=plt.subplots(len(res),1,figsize=(11,2.2*len(res)),sharex=True)
for a,(fn,(f,db)) in zip(np.atleast_1d(ax),res.items()):
    a.plot(f/1e6,db,lw=.6); a.set_ylabel(fn.split('/')[-1].replace('.bin',''))
np.atleast_1d(ax)[-1].set_xlabel('MHz (po próbkowaniu ~9.1 MS/s)')
plt.tight_layout(); plt.savefig(os.environ.get('OUT','adc_spectrum.png'),dpi=90)
