"""Vocal note detection on a separated vocal stem: syllable onsets, sung pitch, sustained notes.
Used by make_song.py; numpy only."""
import numpy as np

def yin(x, sr, hop, fmin=80.0, fmax=1000.0, W=1024):
    """YIN pitch track. Returns (f0 Hz per frame, aperiodicity 0..1 per frame)."""
    tmin, tmax = int(sr / fmax), int(sr / fmin)
    nfr = 1 + (len(x) - W - tmax) // hop
    f0 = np.zeros(nfr); ap = np.ones(nfr)
    L = W + tmax
    nfft = 1 << int(np.ceil(np.log2(L + W)))
    for s in range(0, nfr, 1024):
        e = min(nfr, s + 1024)
        idx = np.arange(L)[None, :] + hop * np.arange(s, e)[:, None]
        fr = x[idx].astype(np.float64)
        # d(tau) = sum_{j<W} (x_j - x_{j+tau})^2 = E0 + E_tau - 2 r(tau)
        F = np.fft.rfft(fr, nfft); G = np.fft.rfft(fr[:, :W], nfft)
        r = np.fft.irfft(F * np.conj(G), nfft)[:, :tmax + 1]
        sq = np.cumsum(np.concatenate([np.zeros((e - s, 1)), fr ** 2], 1), 1)
        E0 = sq[:, W][:, None]
        tau = np.arange(tmax + 1)
        Et = sq[:, tau + W] - sq[:, tau]
        d = E0 + Et - 2 * r
        d[:, 0] = 0
        cm = d[:, 1:] * np.arange(1, tmax + 1) / np.maximum(np.cumsum(d[:, 1:], 1), 1e-12)   # cumulative-mean normalised
        cm = np.concatenate([np.ones((e - s, 1)), cm], 1)
        for k in range(e - s):
            c = cm[k, tmin:tmax]
            below = np.where(c < 0.15)[0]
            if len(below):
                i = below[0]
                while i + 1 < len(c) and c[i + 1] < c[i]: i += 1
            else:
                i = int(np.argmin(c))
            t = i + tmin
            if 1 <= t < tmax:                        # parabolic refinement
                a_, b_, c_ = cm[k, t - 1], cm[k, t], cm[k, t + 1]
                den = a_ - 2 * b_ + c_
                t = t + (0.5 * (a_ - c_) / den if abs(den) > 1e-9 else 0)
            f0[s + k] = sr / t; ap[s + k] = cm[k, int(round(t))] if int(round(t)) <= tmax else 1
    return f0, ap


def vocal_notes(x, sr, hop):
    """Segments the vocal stem into sung notes. Returns list of dicts: t0, t1 (s), midi, strength."""
    f0, ap = yin(x, sr, hop)
    n = len(f0)
    fr = x[: n * hop].reshape(n, hop)
    rms = np.sqrt((fr ** 2).mean(1)) + 1e-9
    db = 20 * np.log10(rms)
    # loudness relative to the surrounding 4 s, so quiet verses still count
    win = int(4 * sr / hop)
    pad = np.pad(db, win, mode="edge")
    locmax = np.array([pad[i: i + 2 * win].max() for i in range(0, n)])
    voiced = (db > locmax - 24) & (db > db.max() - 48) & (ap < 0.3)
    # close tiny gaps / drop blips
    def runs(b):
        d = np.diff(np.concatenate([[0], b.astype(int), [0]]))
        return list(zip(np.where(d == 1)[0], np.where(d == -1)[0]))
    for a, b in runs(~voiced):
        if b - a <= 3 and a > 0 and b < n: voiced[a:b] = True
    for a, b in runs(voiced):
        if b - a < 5: voiced[a:b] = False
    midi = np.where(voiced, 69 + 12 * np.log2(np.maximum(f0, 1) / 440.0), np.nan)
    # median-smooth the pitch inside voiced runs
    sm = midi.copy()
    for i in range(n):
        if voiced[i]:
            seg = midi[max(0, i - 3): i + 4]
            sm[i] = np.nanmedian(seg)
    # syllable onsets: rises in vocal loudness (consonant/attack), in dB
    rise = np.maximum(0, np.convolve(db, [1, 1, 0, -1, -1], "same"))
    notes = []
    for a, b in runs(voiced):
        cuts = [a]
        # split on pitch steps that hold, and on loudness re-attacks
        i = a + 4
        while i < b - 4:
            before = np.nanmedian(sm[max(a, i - 4): i]); after = np.nanmedian(sm[i: i + 4])
            if abs(after - before) > 0.9 or (rise[i] > 9 and rise[i] == rise[max(a, i - 3): i + 4].max()):
                if i - cuts[-1] >= 5:
                    cuts.append(i); i += 5; continue
            i += 1
        cuts.append(b)
        for c0, c1 in zip(cuts[:-1], cuts[1:]):
            if c1 - c0 < 4: continue
            p = np.nanmedian(sm[c0:c1])
            notes.append(dict(t0=c0 * hop / sr, t1=c1 * hop / sr, midi=float(p),
                              strength=float(db[c0: c0 + 6].max() - locmax[c0] + 24 + rise[max(0, c0 - 2): c0 + 3].max())))
    return notes
