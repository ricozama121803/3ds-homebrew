#!/usr/bin/env python3
"""Turns an audio file into a BeatPad song folder: streamable PCM plus auto-generated note charts.

    python3 tools/make_song.py <audio file> <out dir> --title "ALV" [--artist "..."]

Writes into <out dir>:
    audio.bpa      the music, 32 kHz stereo IMA-ADPCM (tools/bpa.py; streamed and decoded on the 3DS)
    info.txt       key=value metadata (title, artist, bpm, length, preview start, note counts)
    beats.txt      one beat per line: "<time ms> <1 if downbeat else 0>" (drawn as lines on the highway)
    easy.chart     highway notes: "<time ms> <lane 0-5> <hold length ms>" per line
    normal.chart
    hard.chart

Lanes, left to right: 0 = D-pad Left, 1 = D-pad Up, 2 = D-pad Right, 3 = Y, 4 = X, 5 = A.
Easy uses lanes 0, 2, 3, 5 only; Normal and Hard use all six. A chord never puts two notes under one thumb.

Needs ffmpeg and numpy. With --separate (or --stems DIR holding vocals.wav + no_vocals.wav) the highway follows
the singing: each sung syllable is a note at its real time, long sung notes are holds, lanes follow the sung
melody, and instrumental gaps are filled from the drums. Without stems the highway is charted from the full mix on a 16th-note grid.
"""
import argparse, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np

SR = 22050          # analysis rate
HOP = 256           # analysis hop (11.6 ms)
NFFT = 2048
OUT_RATE = 32000    # playback rate on the 3DS


def decode(path, rate, channels, fmt):
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-ac", str(channels), "-ar", str(rate), "-f", fmt, "-"]
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout


def stft_mag(x, nfft=NFFT, hop=HOP):
    w = np.hanning(nfft).astype(np.float32)
    frames = 1 + (len(x) - nfft) // hop
    out = np.empty((frames, nfft // 2 + 1), np.float32)
    for s in range(0, frames, 2048):                      # chunked to keep memory low
        e = min(frames, s + 2048)
        idx = np.arange(nfft)[None, :] + hop * np.arange(s, e)[:, None]
        out[s:e] = np.abs(np.fft.rfft(x[idx] * w, axis=1))
    return out


def band_matrix(nbins, lo, hi, nbands):
    """Triangular log-spaced filterbank (nbands x nbins)."""
    freqs = np.arange(nbins) * SR / NFFT
    edges = np.geomspace(lo, hi, nbands + 2)
    m = np.zeros((nbands, nbins), np.float32)
    for b in range(nbands):
        l, c, r = edges[b], edges[b + 1], edges[b + 2]
        up = (freqs - l) / (c - l); down = (r - freqs) / (r - c)
        m[b] = np.clip(np.minimum(up, down), 0, None)
    return m, edges[1:-1]


def flux(bands):
    L = np.log1p(100.0 * bands)
    f = np.maximum(0.0, np.diff(L, axis=0)).sum(1)
    return np.concatenate([[0.0], f])


def smooth(v, n):
    k = np.hanning(n * 2 + 1); k /= k.sum()
    return np.convolve(v, k, "same")


def tempo_estimate(env, fps, lo=70, hi=180):
    e = env - env.mean()
    ac = np.correlate(e, e, "full")[len(e) - 1:]
    best, best_bpm = -1e30, 120.0
    for bpm in np.arange(lo, hi, 0.05):
        lag = 60.0 * fps / bpm
        score = 0.0
        for mult, wgt in ((1, 1.0), (2, 0.5), (4, 0.25)):     # reward periodicity at 1, 2 and 4 beats
            lg = lag * mult; l = int(lg); f = lg - l
            if l + 1 < len(ac): score += wgt * (ac[l] * (1 - f) + ac[l + 1] * f)
        # gentle preference for 80-140 BPM so we don't lock onto double/half time
        score *= np.exp(-0.5 * (np.log2(bpm / 110.0) / 0.9) ** 2)
        if score > best: best, best_bpm = score, bpm
    return best_bpm


def track_beats(env, fps, bpm, tightness=120.0):
    """Ellis (2007) dynamic-programming beat tracker."""
    period = 60.0 * fps / bpm
    e = env / (env.std() + 1e-9)
    n = len(e)
    score = e.copy(); back = -np.ones(n, int)
    lo, hi = int(round(period / 2)), int(round(period * 2))
    offs = np.arange(lo, hi + 1)
    pen = -tightness * np.log(offs / period) ** 2
    for i in range(hi, n):
        cand = score[i - offs] + pen
        j = np.argmax(cand)
        score[i] = e[i] + cand[j]; back[i] = i - offs[j]
    # start from the best-scoring frame within the last period
    i = n - hi + int(np.argmax(score[n - hi:]))
    beats = []
    while i >= 0:
        beats.append(i)
        i = back[i]
    return np.array(beats[::-1])


def fit_constant_grid(env, low, fps, bpm, t0, t1):
    """Best constant-tempo beat grid: search period near the estimate and phase, scoring onset strength on 8ths.
    Returns (period s, first-beat phase s), with the beat (not the off-beat 8th) chosen by kick energy."""
    e = env / (env.std() + 1e-9)
    m = np.maximum(e, np.maximum(np.roll(e, 1), np.roll(e, -1)))

    def score(P, ph):
        ts = np.arange(ph, t1, P / 2); ts = ts[ts >= t0]
        return m[np.minimum(np.round(ts * fps).astype(int), len(m) - 1)].mean()

    P0 = 60.0 / bpm
    best = (-1, P0, 0)
    for P in np.arange(P0 * 0.985, P0 * 1.015, 0.0002):
        for ph in np.arange(0, P / 2, 0.004):
            s = score(P, ph)
            if s > best[0]: best = (s, P, ph)
    _, P, ph = best
    for P2 in np.arange(P - 0.0003, P + 0.0003, 0.00002):         # refine
        for ph2 in np.arange(ph - 0.006, ph + 0.006, 0.001):
            s = score(P2, ph2)
            if s > best[0]: best = (s, P2, ph2)
    _, P, ph = best
    lw = low / (low.std() + 1e-9)
    def kick(phase):
        ts = np.arange(phase, t1, P); ts = ts[ts >= t0]
        return lw[np.minimum(np.round(ts * fps).astype(int), len(lw) - 1)].mean()
    if kick(ph + P / 2) > kick(ph): ph += P / 2
    return P, ph


def align_to(mix, stem, sr=SR):
    """Demucs keeps the MP3 decoder's start-up delay that ffmpeg trims, so its stems run ~23 ms late.
    Finds the lag by cross-correlating 20 s from the middle of the song and returns the stem shifted into line."""
    n = len(mix); mid = n // 2; w = 20 * sr; r = sr // 5               # search +-200 ms
    a = mix[mid: mid + w].astype(np.float64)
    b = stem[max(0, mid - r): mid + w + r].astype(np.float64)
    nfft = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    c = np.fft.irfft(np.fft.rfft(b, nfft) * np.conj(np.fft.rfft(a, nfft)), nfft)[: len(b) - len(a) + 1]
    lag = int(np.argmax(c)) - (mid - max(0, mid - r))
    out = stem[lag:] if lag >= 0 else np.concatenate([np.zeros(-lag, stem.dtype), stem])
    return np.pad(out, (0, max(0, n - len(out))))[:n], lag


def fine_onsets(x, sr=SR, hop=64):
    """Drum/attack onsets with ~3 ms precision: log-band spectral flux at a 64-sample hop, local peaks,
    parabolic interpolation. Returns (times s, strengths)."""
    S = stft_mag(x, 1024, hop)
    fb, _ = band_matrix(S.shape[1], 30, 10000, 40)
    fb = fb[:, : S.shape[1]]
    e = flux(S @ fb.T)
    e = e / (np.percentile(e, 99) + 1e-9)
    w = 6                                                       # peaks must win +-6 frames (~17 ms)
    pad = np.pad(e, w, mode="constant")
    win = np.lib.stride_tricks.sliding_window_view(pad, 2 * w + 1)
    loc = np.convolve(e, np.ones(int(sr / hop)) / int(sr / hop), "same")       # 1 s average
    pk = np.where((e == win.max(1)) & (e > 2.0 * loc) & (e > 0.08))[0]
    pk = pk[(pk > 0) & (pk < len(e) - 1)]
    a_, b_, c_ = e[pk - 1], e[pk], e[pk + 1]
    den = a_ - 2 * b_ + c_
    sh = np.where(den < -1e-9, 0.5 * (a_ - c_) / np.where(den < -1e-9, den, 1), 0)
    # STFT frame i covers samples [i*hop, i*hop+1024): the attack sits near the frame's centre
    t = (pk + np.clip(sh, -1, 1)) * hop / sr + 512 / sr
    return t, e[pk]


def refine_grid(P, ph, ot, ow, t0, t1):
    """Re-fits the beat grid to the onsets: each 8th-note position takes the strongest onset near it, then a
    weighted line through them gives the tempo and phase (sub-millisecond; a wrong period otherwise drifts by
    the length of the song). Repeats with a narrowing window. Returns (P, ph, matched 8th indices, residuals)."""
    for win in (0.22, 0.16, 0.12, 0.10, 0.10):
        h = P / 2
        j = np.arange(int(np.floor((t0 - ph) / h)), int((t1 - ph) / h) + 1)
        pred = ph + j * h
        lo = np.searchsorted(ot, pred - win * P); hi = np.searchsorted(ot, pred + win * P)
        J, T, W = [], [], []
        for k in range(len(j)):
            if hi[k] > lo[k]:
                m = lo[k] + int(np.argmax(ow[lo[k]: hi[k]]))
                J.append(j[k]); T.append(ot[m]); W.append(ow[m] * (2.0 if j[k] % 2 == 0 else 1.0))
        J, T, W = np.array(J, float), np.array(T), np.array(W)
        A = np.stack([np.ones_like(J), J * 0.5], 1) * W[:, None]
        sol = np.linalg.lstsq(A, T * W, rcond=None)[0]
        ph, P = sol[0], sol[1]
    return P, ph, J, T - (ph + J * P / 2), W


def peak_salience_pitch(spec_frame, freqs):
    """Rough dominant pitch (MIDI float) from a magnitude spectrum, via harmonic summation over 90-1000 Hz."""
    cands = np.geomspace(90, 1000, 240)
    sal = np.zeros(len(cands))
    df = freqs[1]
    for h, wgt in ((1, 1.0), (2, 0.6), (3, 0.4), (4, 0.25), (5, 0.15)):
        bins = np.clip(np.round(cands * h / df).astype(int), 0, len(spec_frame) - 1)
        sal += wgt * spec_frame[bins]
    f0 = cands[int(np.argmax(sal))]
    return 69 + 12 * np.log2(f0 / 440.0)


LANES6 = [0, 1, 2, 3, 4, 5]
LANES4 = [0, 2, 3, 5]

# highway: notes per second, minimum gap between notes (s), shortest sung note that becomes a hold (s)
HIGHWAY = {
    "easy":   dict(nps=1.4, gap=0.32, lanes=LANES4, hold=0.60, chords=0.00),
    "normal": dict(nps=2.4, gap=0.20, lanes=LANES6, hold=0.45, chords=0.03),
    "hard":   dict(nps=3.5, gap=0.13, lanes=LANES6, hold=0.40, chords=0.08),
}


def contour_lanes(times, pitch, nl):
    """Lane index per note from the melody: local pitch rank, keeping the up/down direction between notes."""
    times = np.asarray(times); pitch = np.asarray(pitch)
    out = []
    prev_lane, prev_pitch = nl // 2, None
    for i in range(len(times)):
        nb = np.abs(times - times[i]) < 6.0
        q = (np.sum(pitch[nb] < pitch[i]) + 0.5 * np.sum(pitch[nb] == pitch[i])) / nb.sum()
        li = min(nl - 1, int(q * nl))
        if prev_pitch is not None:
            dp = pitch[i] - prev_pitch
            if abs(dp) < 0.6: li = prev_lane
            elif dp > 0 and li <= prev_lane: li = min(nl - 1, prev_lane + 1)
            elif dp < 0 and li >= prev_lane: li = max(0, prev_lane - 1)
        out.append(li); prev_lane, prev_pitch = li, pitch[i]
    return out


def greedy_pick(times, score, gap, target, floor=0.0):
    """Strongest-first selection with a minimum gap. Returns sorted indices."""
    order = np.argsort(-np.asarray(score))
    taken_t, taken_i = [], []
    for i in order:
        if len(taken_i) >= target or score[i] < floor: break
        t = times[i]
        j = np.searchsorted(taken_t, t)
        if j > 0 and t - taken_t[j - 1] < gap - 1e-3: continue
        if j < len(taken_t) and taken_t[j] - t < gap - 1e-3: continue
        taken_t.insert(j, t); taken_i.insert(j, i)
    return taken_i


def write_chart(path, notes):
    notes = sorted(notes, key=lambda n: (n[0], n[1]))
    with open(path, "w") as f:
        for t, ln, h in notes:
            f.write(f"{int(round(t * 1000))} {ln} {int(round(max(0, h) * 1000))}\n")
    return notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio"); ap.add_argument("outdir")
    ap.add_argument("--title", required=True); ap.add_argument("--artist", default="")
    ap.add_argument("--bpm", type=float, default=0, help="override the tempo estimate")
    ap.add_argument("--variable-tempo", action="store_true", help="track drifting tempo (live recordings) instead of fitting one grid")
    ap.add_argument("--stems", help="folder with vocals.wav and no_vocals.wav (from demucs)")
    ap.add_argument("--separate", action="store_true", help="run demucs (tools/venv) to split the vocals first; cached in .stems/")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    here = os.path.dirname(os.path.abspath(__file__))

    stems = a.stems
    if a.separate and not stems:
        name = os.path.splitext(os.path.basename(a.audio))[0]
        cache = os.path.join(here, "..", ".stems")
        stems = os.path.join(cache, "htdemucs", name)
        if not os.path.exists(os.path.join(stems, "vocals.wav")):
            py = os.path.join(here, "..", "..", "tools", "venv", "bin", "python")
            print("separating vocals with demucs (a few minutes)...")
            subprocess.run([py, "-m", "demucs", "--two-stems=vocals", "-n", "htdemucs", "-o", cache, a.audio], check=True)

    # ---- audio for the 3DS ----
    import bpa
    pcm = decode(a.audio, OUT_RATE, 2, "s16le")
    with open(os.path.join(a.outdir, "audio.bpa"), "wb") as f: f.write(bpa.encode(np.frombuffer(pcm, np.int16).reshape(-1, 2)))
    old = os.path.join(a.outdir, "audio.pcm")
    if os.path.exists(old): os.remove(old)                  # older uncompressed format
    length_ms = int(len(pcm) / 4 / OUT_RATE * 1000)

    # ---- analysis of the full mix: tempo grid ----
    x = np.frombuffer(decode(a.audio, SR, 1, "f32le"), np.float32)
    fps = SR / HOP
    S = stft_mag(x)
    freqs = np.arange(S.shape[1]) * SR / NFFT
    fb, centers = band_matrix(S.shape[1], 30, 10000, 48)
    B = S @ fb.T
    env = flux(B)
    low = flux(B[:, centers < 160])
    rms = np.sqrt(smooth((x[: len(S) * HOP].reshape(len(S), HOP) ** 2).mean(1), int(fps)))
    midE = smooth(B[:, (centers >= 160) & (centers < 2500)].sum(1), 3)

    bpm = a.bpm or tempo_estimate(smooth(env, 2), fps)
    loud = rms > 0.15 * np.median(rms)
    first, last = np.argmax(loud), len(loud) - 1 - np.argmax(loud[::-1])
    xi = x
    if stems:
        xi, lag = align_to(x, np.frombuffer(decode(os.path.join(stems, "no_vocals.wav"), SR, 1, "f32le"), np.float32))
        print(f"stems are {lag / SR * 1000:+.1f} ms off the mix: corrected")
    if a.variable_tempo:
        beats = track_beats(smooth(env, 1), fps, bpm)
        beats = beats[(beats > 0)]
    else:
        P, ph = fit_constant_grid(env, low, fps, bpm, first / fps, last / fps)
        # sharpen it on precise drum onsets (the instrumental stem when we have one)
        ot, ow = fine_onsets(xi)
        P, ph, J, res, W = refine_grid(P, ph, ot, ow, first / fps, last / fps)
        matched_t = ph + J * P / 2 + res                  # the drum hits the grid was fitted to
        ph = ph % P
        bpm = 60.0 / P
        bt_exact = ph + np.arange(0, int(len(env) / fps / P) + 1) * P
        # small real tempo changes: weighted median residual of the hits within +-8 beats, applied if it matters
        bt_res = np.zeros(len(bt_exact))
        for k, tb in enumerate(bt_exact):
            m = np.abs(matched_t - tb) < 8 * P
            if m.sum() >= 6:
                r, w = res[m], W[m]
                o = np.argsort(r); cw = np.cumsum(w[o])
                bt_res[k] = r[o][np.searchsorted(cw, cw[-1] / 2)]
        if np.abs(bt_res).max() > 0.012: bt_exact = bt_exact + bt_res
        print(f"grid fit: {len(res)} drum hits matched, median residual {np.median(np.abs(res)) * 1000:.1f} ms, "
              f"tempo drift correction up to {np.abs(bt_res).max() * 1000:.0f} ms")
        beats = np.round(bt_exact * fps).astype(int)
    keep = (beats >= first - fps * 0.2) & (beats <= last)
    beats = beats[keep]
    bt = bt_exact[keep] if not a.variable_tempo else beats / fps
    lowsum = [sum(low[b] for b in beats[p::4]) / max(1, len(beats[p::4])) for p in range(4)]
    phase = int(np.argmax(lowsum))
    beat_len = 60.0 / bpm
    print(f"tempo {bpm:.2f} BPM, {len(bt)} beats, first beat {bt[0]:.3f}s, downbeat phase {phase}")
    with open(os.path.join(a.outdir, "beats.txt"), "w") as f:
        for i, t in enumerate(bt):
            f.write(f"{int(round(t * 1000))} {1 if (i - phase) % 4 == 0 else 0}\n")

    # ---- drum hits (instrumental gaps): on the instrumental stem when we have it ----
    if stems:
        Si = stft_mag(xi); Bi = Si @ fb.T
    else:
        Bi = B
    kick = flux(Bi[:, centers < 150]); snare = flux(Bi[:, (centers >= 1500) & (centers < 6000)])
    drum = flux(Bi)
    def norm(v): return v / (np.percentile(v, 99) + 1e-9)
    kick, snare, drum = norm(kick), norm(snare), norm(drum)
    def at(v, t, w=0.035):
        f0 = int(t * fps); k = int(w * fps)
        return float(v[max(0, f0 - k): f0 + k + 1].max()) if f0 < len(v) else 0.0

    # 16th grid with drum strength (used for instrumental fill)
    grid, gpos = [], []
    for i in range(len(bt) - 1):
        for k in range(4):
            grid.append(bt[i] + (bt[i + 1] - bt[i]) * k / 4); gpos.append(k)
    grid = np.array(grid); gpos = np.array(gpos)
    gstr = np.array([at(drum, t) for t in grid]) * np.where(gpos == 0, 1.25, np.where(gpos == 2, 1.05, 0.85))
    gpitch = np.zeros(len(grid))
    for i, t in enumerate(grid):
        g = int(t * fps); seg = S[g: g + int(0.08 * fps) + 1]
        gpitch[i] = peak_salience_pitch(seg.mean(0) if len(seg) else S[-1], freqs)

    # ---- sung notes ----
    vnotes = []
    if stems:
        import vocals
        xv, _ = align_to(x - xi, np.frombuffer(decode(os.path.join(stems, "vocals.wav"), SR, 1, "f32le"), np.float32))
        vnotes = vocals.vocal_notes(xv, SR, HOP)
        print(f"{len(vnotes)} sung notes")
    vt0 = np.array([n["t0"] for n in vnotes]); vt1 = np.array([n["t1"] for n in vnotes])
    vmidi = np.array([n["midi"] for n in vnotes]); vstr = np.array([n["strength"] for n in vnotes])

    counts = {}
    dur = bt[-1] - bt[0]
    rng = np.random.default_rng(7)

    # ================= highway =================
    # Candidates are sung syllables (snapped onto the 16th grid when they're within 45 ms of it) and drum hits on
    # the grid. Strong metrical positions score higher; drums score lower than singing except where nobody sings.
    # Notes are then chosen two bars at a time, with each section's share set by how loud it is, so density
    # follows the song's energy instead of bunching up.
    METRIC = {0: 1.3, 2: 1.1, 1: 0.9, 3: 0.9}
    beat_idx = np.repeat(np.arange(len(bt) - 1), 4)            # beat number of each grid point
    is_down = ((beat_idx - phase) % 4 == 0) & (gpos == 0)
    def nearest(t):                                             # index of the closest grid point
        j = int(np.clip(np.searchsorted(grid, t), 1, len(grid) - 1))
        return j - 1 if abs(grid[j - 1] - t) < abs(grid[j] - t) else j
    kickness = np.array([at(kick, t) for t in grid]); snareness = np.array([at(snare, t) for t in grid])
    sung = np.zeros(len(grid), bool)
    for t0, t1 in zip(vt0, vt1): sung |= (grid > t0 - beat_len * 0.6) & (grid < t1 + beat_len * 0.6)

    # sung candidates
    V = {}                                                      # grid index (or -1 - k for off-grid) -> candidate
    if len(vnotes):
        prev_gap = np.concatenate([[9], vt0[1:] - vt1[:-1]])
        dp = np.abs(np.concatenate([[0], np.diff(vmidi)]))
        base = (vstr / np.percentile(vstr, 90)) + 0.6 * np.clip((vt1 - vt0) / 0.5, 0, 1.5) \
               + 0.5 * (prev_gap > 0.2) + 0.25 * np.clip(dp / 3, 0, 1)
        for k in range(len(vt0)):
            gi = nearest(vt0[k])
            if abs(vt0[k] - grid[gi]) <= 0.045:
                key, t, sc = gi, grid[gi], base[k] * METRIC[gpos[gi]]
            else:
                key, t, sc = -1 - k, vt0[k], base[k] * 0.5      # off the grid: usually detection noise
            c = dict(t=t, s=sc, pitch=vmidi[k], end=vt1[k], vocal=True, drum=None)
            if key not in V or V[key]["s"] < sc: V[key] = c
    cands = list(V.values())
    # drum candidates on the grid
    for gi in range(len(grid)):
        if gi in V: continue
        dscore = max(kickness[gi], snareness[gi]) * METRIC[gpos[gi]] * (1.9 if not sung[gi] else 0.55)
        cands.append(dict(t=grid[gi], s=dscore, pitch=gpitch[gi], end=grid[gi], vocal=False,
                          drum="kick" if kickness[gi] >= snareness[gi] else "snare", gpos=gpos[gi], down=bool(is_down[gi])))
    cands.sort(key=lambda c: c["t"])
    ct = np.array([c["t"] for c in cands])

    # section loudness -> share of the note budget
    downs = [bt[i] for i in range(len(bt)) if (i - phase) % 4 == 0]
    edges = [bt[0]] + downs[1::2] + [bt[-1] + 1e-3]
    sec_rms = np.array([rms[int(e0 * fps): max(int(e0 * fps) + 1, int(e1 * fps))].mean() for e0, e1 in zip(edges[:-1], edges[1:])])
    weight = np.clip((sec_rms / np.median(sec_rms[sec_rms > 0.3 * np.median(sec_rms)])) ** 0.8, 0.05, 1.25)
    weight *= np.array([e1 - e0 for e0, e1 in zip(edges[:-1], edges[1:])])

    for name, d in HIGHWAY.items():
        lanes = d["lanes"]; nl = len(lanes)
        budget = d["nps"] * dur
        quota = budget * weight / weight.sum()
        taken = []                                              # sorted times
        chosen = []
        for (e0, e1), q in zip(zip(edges[:-1], edges[1:]), quota):
            idx = np.where((ct >= e0) & (ct < e1))[0]
            got = 0
            for i in sorted(idx, key=lambda i: -cands[i]["s"]):
                if got >= round(q) or cands[i]["s"] < 0.25: break
                if not cands[i]["vocal"] and (name == "easy" and cands[i]["gpos"] % 2) : continue   # easy: drums on 8ths only
                t = ct[i]
                j = np.searchsorted(taken, t)
                if j > 0 and t - taken[j - 1] < d["gap"] - 1e-3: continue
                if j < len(taken) and taken[j] - t < d["gap"] - 1e-3: continue
                taken.insert(j, t); chosen.append(i); got += 1
        chosen.sort(key=lambda i: ct[i])

        # lanes: sung notes follow the melody across all lanes; drum notes put kicks under the left thumb and
        # snares under the right, stepping across that hand's lanes so beats read as a pattern
        left, right = [l for l in lanes if l < 3], [l for l in lanes if l >= 3]
        vsel = [i for i in chosen if cands[i]["vocal"]]
        vl = dict(zip(vsel, contour_lanes([ct[i] for i in vsel], [cands[i]["pitch"] for i in vsel], nl)))
        step = {"kick": 0, "snare": 0}
        notes = []
        for k, i in enumerate(chosen):
            c = cands[i]; t = ct[i]
            nxt = ct[chosen[k + 1]] if k + 1 < len(chosen) else t + 10
            hold = 0.0
            if c["vocal"]:
                lane = lanes[vl[i]]
                if c["end"] - t >= d["hold"]:
                    hold = min(c["end"] - t, nxt - t - 0.15)
                    if hold < 0.3: hold = 0.0
            else:
                hand = left if c["drum"] == "kick" else right
                if c.get("down"): step[c["drum"]] = 0                  # each bar's pattern restarts on the downbeat
                lane = hand[[0, len(hand) - 1, len(hand) // 2][step[c["drum"]] % 3] if len(hand) > 2 else step[c["drum"]] % len(hand)]
                step[c["drum"]] += 1
            notes.append([t, lane, hold])
            # chords: a strong kick under a downbeat note doubles it on the other hand
            gi = nearest(t)
            if d["chords"] and hold == 0 and is_down[gi] and abs(grid[gi] - t) < 0.002 and kickness[gi] > 0.5 and rng.random() < d["chords"] * 6:
                other = right if lane < 3 else left
                notes.append([t, other[len(other) // 2], 0.0])
        notes = write_chart(os.path.join(a.outdir, f"{name}.chart"), notes)
        counts[name] = len(notes)
        on_grid = np.mean([np.min(np.abs(grid - n[0])) < 0.003 for n in notes]) * 100
        print(f"highway {name:7s} {len(notes):4d} notes ({len(notes) / dur:.2f}/s), {sum(1 for n in notes if n[2] > 0)} holds, "
              f"{sum(cands[i]['vocal'] for i in chosen) * 100 // max(1, len(chosen))}% sung, {on_grid:.0f}% on the 16th grid")

    w = int(15 * fps)
    rs = np.convolve(rms, np.ones(w), "valid")
    q = len(rs) // 4
    preview = (q + int(np.argmax(rs[q:]))) / fps
    with open(os.path.join(a.outdir, "info.txt"), "w") as f:
        f.write(f"title={a.title}\nartist={a.artist}\nbpm={bpm:.2f}\nlength_ms={length_ms}\npreview_ms={int(preview * 1000)}\n")
        for k, v in counts.items(): f.write(f"notes_{k}={v}\n")


if __name__ == "__main__":
    main()
