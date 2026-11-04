#!/usr/bin/env python3
"""fm_demod.py IN.iq|IN.i8 OUT_PREFIX: FM-demodulate an fmrx recording. Prints channel SNR, pilot and RDS tests; writes OUT_PREFIX.wav and OUT_PREFIX.png."""
import sys
import numpy as np
from scipy import signal
from scipy.io import wavfile

fn, out = sys.argv[1], sys.argv[2]
d = open(fn, 'rb').read()
bin_ticks, nb, samples = np.frombuffer(d[:12], '<u4')
fs = 276e6 / bin_ticks
raw = np.frombuffer(d[12:], np.int8 if fn.endswith('.i8') else '<i2').astype(float)
x = raw[0::2] + 1j * raw[1::2]
x = x[int(fs * 0.02):]
empty = np.mean(np.abs(x) == 0)
print(f'{len(x)} bins at {fs/1e3:.1f} kS/s, {len(x)/fs:.2f} s, empty bins {empty*100:.2f} %')

# hold over empty bins (interrupt gaps) so they do not click as much
idx = np.where(np.abs(x) > 0)[0]
x = np.interp(np.arange(len(x)), idx, x[idx].real) + 1j * np.interp(np.arange(len(x)), idx, x[idx].imag)

f, p = signal.welch(x, fs, nperseg=4096, return_onesided=False, detrend=False)
o = np.argsort(f); f, p = f[o], p[o]
db = 10 * np.log10(p)
inb = np.abs(f) < 80e3
oob = np.abs(f) > min(95e3, 0.8 * fs / 2)
print(f'in channel {10*np.log10(p[inb].mean()):.1f} dB, outside {10*np.log10(p[oob].mean()):.1f} dB, difference {10*np.log10(p[inb].mean()/p[oob].mean()):.2f} dB')

# weak station: only about +-40 kHz of it rises over the noise
y = signal.filtfilt(signal.firwin(201, 45e3, fs=fs), 1, x)
dem = np.angle(y[1:] * np.conj(y[:-1])) * fs / (2 * np.pi)
fa, pa = signal.welch(dem, fs, nperseg=min(1 << 15, len(dem)))
dba = 10 * np.log10(pa)


def excess(f0, w=150):
    k = np.argmin(np.abs(fa - f0))
    near = (np.abs(fa - f0) > w) & (np.abs(fa - f0) < 800)
    return dba[k-1:k+2].max() - np.median(dba[near]), fa[k-1:k+2][np.argmax(dba[k-1:k+2])]


for name, f0 in (('pilot 19 kHz', 19e3), ('test 17 kHz', 17e3), ('test 23 kHz', 23e3), ('38 kHz', 38e3), ('RDS 57 kHz', 57e3)):
    e, fp = excess(f0)
    print(f'{name}: {e:+.1f} dB over neighbours (peak at {fp:.0f} Hz)')

bl, al = signal.butter(4, [80, 7000], 'band', fs=fs)
a = signal.filtfilt(bl, al, dem)
de = np.exp(-1 / (fs * 50e-6))
a = signal.lfilter([1 - de], [1, -de], a)
a = signal.resample_poly(a, 48000, int(round(fs)))
a = np.clip(a / (np.percentile(np.abs(a), 99.7) + 1), -1, 1)
wavfile.write(out + '.wav', 48000, (a * 32000).astype(np.int16))

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig, ax = plt.subplots(2, 1, figsize=(12, 6))
ax[0].plot(f / 1e3, db, lw=.6); ax[0].set_xlabel('kHz od nośnej'); ax[0].set_ylabel('dB'); ax[0].grid(alpha=.3)
ax[1].plot(fa / 1e3, dba, lw=.5); ax[1].set_xlim(0, 80); ax[1].set_xlabel('kHz po demodulacji'); ax[1].set_ylabel('dB'); ax[1].grid(alpha=.3)
for v in (15, 19, 38, 57):
    ax[1].axvline(v, color='r', lw=.4)
plt.tight_layout(); plt.savefig(out + '.png', dpi=80)
