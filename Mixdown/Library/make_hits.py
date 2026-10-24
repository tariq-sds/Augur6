#!/usr/bin/env python3
"""Toota Hits: 64 kicks and 64 claps / snares / rims, synthesised from scratch and level-matched so they
drop into a mix without touching the fader. usage: make_hits.py <out_dir>"""
import sys, os, wave
import numpy as np
from scipy import signal

SR = 48000
NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
rng = np.random.default_rng(20261024)

def note_of(hz):
    m = int(round(69 + 12 * np.log2(hz / 440.0)))
    return NOTES[m % 12]

def env_exp(n, tau):
    t = np.arange(n) / SR
    return np.exp(-t / tau)

def bp(x, lo, hi, order=2):
    sos = signal.butter(order, [lo, hi], btype='band', fs=SR, output='sos')
    return signal.sosfilt(sos, x)

def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype='high', fs=SR, output='sos'), x)

def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype='low', fs=SR, output='sos'), x)

def fade(x, ms=4):
    n = int(SR * ms / 1000)
    r = np.linspace(1, 0, n)
    x[-n:] *= r if x.ndim == 1 else r[:, None]
    return x

def write(path, x):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1: x = x[:, None]
    x = np.clip(x, -1, 1)
    data = (x * 8388607.0).astype(np.int32)
    b = bytearray()
    for frame in data:
        for v in frame:
            b += int(v).to_bytes(4, 'little', signed=True)[:3]
    with wave.open(path, 'wb') as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(3); w.setframerate(SR); w.writeframes(bytes(b))

def peak_norm(x, db=-1.0):
    return x * (10 ** (db / 20) / (np.max(np.abs(x)) + 1e-12))

def loud_norm(x, target_db, win=0.15):
    """match the RMS of the body of the hit, then make sure the peak stays at or under -1 dBFS"""
    n = int(SR * win)
    body = x[:n] if x.ndim == 1 else x[:n].mean(axis=1)
    rms = np.sqrt(np.mean(body ** 2)) + 1e-12
    y = x * (10 ** (target_db / 20) / rms)
    pk = np.max(np.abs(y))
    if pk > 10 ** (-1 / 20): y *= 10 ** (-1 / 20) / pk
    return y

# ------------------------------------------------------------------ kicks
def kick(f_end, f_start, p_tau, a_tau, length, click, drive, sub_tail=0.0, body_q=0.0):
    n = int(SR * length)
    t = np.arange(n) / SR
    f = f_end + (f_start - f_end) * np.exp(-t / p_tau)
    ph = 2 * np.pi * np.cumsum(f) / SR
    amp = np.exp(-t / a_tau) * (1 - sub_tail) + sub_tail * np.exp(-t / (a_tau * 2.5))
    x = np.sin(ph) * amp
    if body_q > 0:   # a little second harmonic body
        x += body_q * np.sin(2 * ph) * np.exp(-t / (a_tau * 0.4))
    # click: short filtered noise burst plus a tiny pitched tick
    nc = int(SR * 0.006)
    c = hp(rng.standard_normal(nc), 2500) * np.exp(-np.arange(nc) / (SR * 0.0012))
    tick = np.sin(2 * np.pi * 1800 * t[:nc]) * np.exp(-np.arange(nc) / (SR * 0.0008))
    x[:nc] += click * (0.6 * c / (np.max(np.abs(c)) + 1e-9) + 0.4 * tick)
    if drive > 0:
        k = 1 + drive * 5
        x = np.tanh(k * x) / np.tanh(k)
    x = hp(x, 22, 2)
    x[:24] *= np.linspace(0, 1, 24) ** 0.5   # no click from a DC step at sample 0
    return fade(peak_norm(x, -1.0), 6)

FAMILIES = [
    # name, f_end range, start mult, pitch tau, amp tau, length, click, drive, sub_tail, body
    ('House', (46, 58), (3.2, 4.0), (0.010, 0.022), (0.16, 0.30), 0.70, (0.25, 0.5), (0.05, 0.25), 0.0, (0.0, 0.08)),
    ('Deep',  (40, 50), (2.6, 3.4), (0.015, 0.035), (0.35, 0.60), 1.30, (0.05, 0.25), (0.0, 0.15), 0.3, (0.0, 0.0)),
    ('Punch', (48, 60), (4.0, 5.5), (0.006, 0.012), (0.14, 0.24), 0.60, (0.4, 0.8), (0.3, 0.7), 0.0, (0.05, 0.15)),
    ('Tight', (52, 64), (3.5, 4.5), (0.005, 0.010), (0.07, 0.13), 0.38, (0.5, 0.9), (0.1, 0.35), 0.0, (0.0, 0.1)),
]

def U(r): return rng.uniform(*r) if isinstance(r, tuple) else r

def make_kicks(out):
    os.makedirs(out, exist_ok=True)
    names = []
    for fam, fe, sm, pt, at, ln, cl, dr, st, bq in FAMILIES:
        for i in range(16):
            f_end = U(fe)
            x = kick(f_end, f_end * U(sm), U(pt), U(at), ln * rng.uniform(0.9, 1.1), U(cl), U(dr), st, U(bq))
            name = f"Toota Kick {fam} {i + 1:02d} ({note_of(f_end)}).wav"
            write(os.path.join(out, name), x); names.append(name)
    return names

# ------------------------------------------------------------------ claps / snares / rims
def stereo_tail(n, tau, lo, hi):
    l = bp(rng.standard_normal(n), lo, hi) * env_exp(n, tau)
    r = bp(rng.standard_normal(n), lo, hi) * env_exp(n, tau)
    return np.stack([l, r], axis=1)

def clap(spacing, bursts, lo, hi, tail_tau, length, width):
    n = int(SR * length)
    x = np.zeros((n, 2))
    for b in range(bursts):
        st = int(SR * spacing * b * rng.uniform(0.8, 1.2))
        m = int(SR * 0.012)
        burst = bp(rng.standard_normal(m), lo, hi) * env_exp(m, 0.0025)
        gain = 0.6 + 0.4 * (b == bursts - 1)
        x[st:st + m, 0] += burst * gain; x[st:st + m, 1] += burst * gain
    st = int(SR * spacing * (bursts - 1))
    tail = stereo_tail(n - st, tail_tau, lo * 0.8, hi * 1.2) * 0.5
    mid = tail.mean(axis=1, keepdims=True)
    x[st:] += mid + (tail - mid) * width
    return x

def snare(tone_hz, tone_tau, noise_tau, noise_lo, length, snap, width):
    n = int(SR * length)
    t = np.arange(n) / SR
    f = tone_hz * (1 + 0.6 * np.exp(-t / 0.008))
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / tone_tau)
    body += 0.5 * np.sin(2 * np.pi * np.cumsum(f * 1.6) / SR) * np.exp(-t / (tone_tau * 0.6))
    nz = stereo_tail(n, noise_tau, noise_lo, 12000)
    mid = nz.mean(axis=1, keepdims=True)
    nz = mid + (nz - mid) * width
    x = body[:, None] * (1 - snap * 0.5) + nz * (0.6 + snap)
    x[:16] *= np.linspace(0, 1, 16)[:, None]
    return x

def rim(f1, f2, tau, length):
    n = int(SR * length)
    t = np.arange(n) / SR
    x = (np.sin(2 * np.pi * f1 * t) * np.exp(-t / tau) + 0.7 * np.sin(2 * np.pi * f2 * t) * np.exp(-t / (tau * 0.6)))
    nc = int(SR * 0.003)
    x[:nc] += hp(rng.standard_normal(nc), 3000) * 0.5
    x = hp(x, 300)
    x[:12] *= np.linspace(0, 1, 12)
    return np.stack([x, x], axis=1)

def make_tops(out):
    os.makedirs(out, exist_ok=True)
    names = []
    for i in range(24):
        x = clap(rng.uniform(0.007, 0.013), int(rng.integers(3, 5)), rng.uniform(700, 1100), rng.uniform(2200, 4500),
                 rng.uniform(0.05, 0.16), rng.uniform(0.35, 0.7), rng.uniform(0.2, 0.9))
        x = fade(loud_norm(hp(x.T, 180).T, -21.5), 8)
        name = f"Toota Clap {i + 1:02d}.wav"; write(os.path.join(out, name), x); names.append(name)
    for i in range(24):
        x = snare(rng.uniform(160, 260), rng.uniform(0.04, 0.10), rng.uniform(0.08, 0.22), rng.uniform(1500, 3500),
                  rng.uniform(0.35, 0.6), rng.uniform(0.0, 0.6), rng.uniform(0.1, 0.6))
        x = fade(loud_norm(hp(x.T, 120).T, -17.5), 8)
        name = f"Toota Snare {i + 1:02d}.wav"; write(os.path.join(out, name), x); names.append(name)
    for i in range(16):
        x = rim(rng.uniform(420, 620), rng.uniform(1500, 2100), rng.uniform(0.012, 0.03), 0.15)
        x = fade(loud_norm(x, -24.0, 0.05), 4)
        name = f"Toota Rim {i + 1:02d}.wav"; write(os.path.join(out, name), x); names.append(name)
    return names

if __name__ == '__main__':
    root = sys.argv[1]
    k = make_kicks(os.path.join(root, 'Kicks'))
    c = make_tops(os.path.join(root, 'Claps Snares Rims'))
    with open(os.path.join(root, 'README.txt'), 'w') as f:
        f.write("Toota Hits - 64 kicks and 64 claps, snares and rims, synthesised from scratch.\n"
                "Kicks are peak-matched at -1 dBFS and named with their key. Claps, snares and rims are each loudness-matched\n"
                "within their set, all peaking under -1 dBFS: swap hits without touching the fader.\n"
                "48 kHz / 24-bit. Free to use in your own releases.\n")
    print(len(k), 'kicks,', len(c), 'claps/snares/rims')
