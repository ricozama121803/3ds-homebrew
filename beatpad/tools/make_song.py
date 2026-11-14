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
    if a.variable_tempo:
        beats = track_beats(smooth(env, 1), fps, bpm)
        beats = beats[(beats > 0)]
    else:
        P, ph = fit_constant_grid(env, low, fps, bpm, first / fps, last / fps)
        bpm = 60.0 / P
        beats = np.round(np.arange(ph, len(env) / fps, P) * fps).astype(int)
        bt_exact = np.arange(ph, len(env) / fps, P)
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
        xi = np.frombuffer(decode(os.path.join(stems, "no_vocals.wav"), SR, 1, "f32le"), np.float32)
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
        xv = np.frombuffer(decode(os.path.join(stems, "vocals.wav"), SR, 1, "f32le"), np.float32)
        vnotes = vocals.vocal_notes(xv, SR, HOP)
        print(f"{len(vnotes)} sung notes")
    vt0 = np.array([n["t0"] for n in vnotes]); vt1 = np.array([n["t1"] for n in vnotes])
    vmidi = np.array([n["midi"] for n in vnotes]); vstr = np.array([n["strength"] for n in vnotes])

    counts = {}
    dur = bt[-1] - bt[0]
    rng = np.random.default_rng(7)

    # ================= highway =================
    for name, d in HIGHWAY.items():
        lanes = d["lanes"]; nl = len(lanes)
        cand_t, cand_s, cand_p, cand_end, cand_vocal = [], [], [], [], []
        if len(vnotes):
            # score: loud attacks, longer notes, phrase starts, melody moves
            prev_gap = np.concatenate([[9], vt0[1:] - vt1[:-1]])
            dp = np.abs(np.concatenate([[0], np.diff(vmidi)]))
            sc = (vstr / np.percentile(vstr, 90)) + 0.6 * np.clip((vt1 - vt0) / 0.5, 0, 1.5) \
                 + 0.5 * (prev_gap > 0.2) + 0.25 * np.clip(dp / 3, 0, 1)
            cand_t += list(vt0); cand_s += list(sc); cand_p += list(vmidi); cand_end += list(vt1); cand_vocal += [True] * len(vt0)
            # instrumental fill where nobody sings for over a beat
            sung = np.zeros(len(grid), bool)
            for t0, t1 in zip(vt0, vt1): sung |= (grid > t0 - beat_len * 0.6) & (grid < t1 + beat_len * 0.6)
            fill = np.where(~sung & (gpos % 2 == 0))[0]
            cand_t += list(grid[fill]); cand_s += list(gstr[fill] * 1.3); cand_p += list(gpitch[fill])
            cand_end += list(grid[fill]); cand_vocal += [False] * len(fill)
        else:
            sub_ok = np.where(gpos % (2 if name != "hard" else 1) == 0)[0]
            cand_t += list(grid[sub_ok]); cand_s += list(gstr[sub_ok]); cand_p += list(gpitch[sub_ok])
            cand_end += list(grid[sub_ok]); cand_vocal += [False] * len(sub_ok)
        cand_t = np.array(cand_t); cand_s = np.array(cand_s); cand_p = np.array(cand_p); cand_end = np.array(cand_end)
        sel = greedy_pick(cand_t, cand_s, d["gap"], int(d["nps"] * dur))
        sel = sorted(sel, key=lambda i: cand_t[i])
        st = cand_t[sel]
        # tidy: notes within 15 ms of a 32nd grid line sit on it
        g32 = beat_len / 8
        st = np.array([bt[0] + round((t - bt[0]) / g32) * g32 if abs(((t - bt[0]) / g32) - round((t - bt[0]) / g32)) * g32 < 0.015 else t for t in st])
        li = contour_lanes(st, cand_p[sel], nl)
        notes = []
        for k, i in enumerate(sel):
            t = st[k]
            nxt = st[k + 1] if k + 1 < len(sel) else t + 10
            hold = 0.0
            if cand_vocal[i] and cand_end[i] - cand_t[i] >= d["hold"]:
                hold = min(cand_end[i] - t, nxt - t - 0.15)
                if hold < 0.3: hold = 0.0
            notes.append([t, lanes[li[k]], hold])
            if d["chords"] and hold == 0 and at(kick, t) > 0.6 and rng.random() < d["chords"] * 4:
                notes.append([t, 5 - lanes[li[k]], 0.0])
        notes = write_chart(os.path.join(a.outdir, f"{name}.chart"), notes)
        counts[name] = len(notes)
        holds = sum(1 for n in notes if n[2] > 0)
        print(f"highway {name:7s} {len(notes):4d} notes ({len(notes) / dur:.2f}/s), {holds} holds")

    w = int(15 * fps)
    rs = np.convolve(rms, np.ones(w), "valid")
    q = len(rs) // 4
    preview = (q + int(np.argmax(rs[q:]))) / fps
    with open(os.path.join(a.outdir, "info.txt"), "w") as f:
        f.write(f"title={a.title}\nartist={a.artist}\nbpm={bpm:.2f}\nlength_ms={length_ms}\npreview_ms={int(preview * 1000)}\n")
        for k, v in counts.items(): f.write(f"notes_{k}={v}\n")


if __name__ == "__main__":
    main()
