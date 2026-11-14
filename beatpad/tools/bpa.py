#!/usr/bin/env python3
"""BeatPad audio (.bpa): stereo IMA-ADPCM, 4 bits per sample (a quarter of raw 16-bit PCM).

Layout (little-endian):
    "BPA1"  u32 sample rate  u32 total frames  u32 frames per block (2048)
    blocks: [left: s16 predictor, u8 step index, u8 0][right: same][left nibbles 1024 B][right nibbles 1024 B]
Each block starts from its own header, so the player can seek to any block. Nibbles are low-then-high per byte;
every one of a block's 2048 samples is coded (the predictor is the state before the first sample).
The encoder runs all blocks in parallel (numpy), since blocks are independent.

    python3 tools/bpa.py encode in.pcm out.bpa      (raw s16le stereo 32 kHz in)
    python3 tools/bpa.py test in.pcm                 (round-trip SNR)
"""
import struct, sys
import numpy as np

BLOCK = 2048
RATE = 32000
STEPS = np.array([7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
                  107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
                  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
                  4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
                  22385, 24623, 27086, 29794, 32767], np.int32)
IDX_ADJ = np.array([-1, -1, -1, -1, 2, 4, 6, 8], np.int32)


def _step_update(pred, idx, code):
    step = STEPS[idx]
    diff = step >> 3
    diff = diff + np.where(code & 4, step, 0) + np.where(code & 2, step >> 1, 0) + np.where(code & 1, step >> 2, 0)
    pred = np.clip(np.where(code & 8, pred - diff, pred + diff), -32768, 32767)
    idx = np.clip(idx + IDX_ADJ[code & 7], 0, 88)
    return pred, idx


def encode(pcm):
    """pcm: int16 array (frames, 2). Returns the .bpa bytes."""
    frames = len(pcm)
    nb = (frames + BLOCK - 1) // BLOCK
    x = np.zeros((nb * BLOCK, 2), np.int32)
    x[:frames] = pcm
    x = x.reshape(nb, BLOCK, 2).transpose(0, 2, 1).reshape(nb * 2, BLOCK)       # one row per (block, channel)
    # initial state: predictor = first sample, step index matched to the opening slope
    pred = x[:, 0].copy()
    d0 = np.abs(np.diff(x[:, :9], axis=1)).mean(1)
    idx = np.clip(np.searchsorted(STEPS, d0 * 1.5), 0, 88).astype(np.int32)
    hdr_pred, hdr_idx = pred.copy(), idx.copy()
    codes = np.zeros(x.shape, np.uint8)
    for i in range(BLOCK):
        step = STEPS[idx]
        diff = x[:, i] - pred
        code = np.where(diff < 0, 8, 0)
        diff = np.abs(diff)
        c = np.zeros_like(diff)
        for bit, sh in ((4, 0), (2, 1), (1, 2)):
            s = step >> sh
            take = diff >= s
            c = c | np.where(take, bit, 0)
            diff = diff - np.where(take, s, 0)
        code = (code | c).astype(np.int32)
        codes[:, i] = code
        pred, idx = _step_update(pred, idx, code)
    out = bytearray(b"BPA1" + struct.pack("<III", RATE, frames, BLOCK))
    packed = (codes[:, 0::2] | (codes[:, 1::2] << 4)).astype(np.uint8)       # (rows, 1024)
    for b in range(nb):
        L, R = 2 * b, 2 * b + 1
        out += struct.pack("<hBBhBB", int(hdr_pred[L]), int(hdr_idx[L]), 0, int(hdr_pred[R]), int(hdr_idx[R]), 0)
        out += packed[L].tobytes() + packed[R].tobytes()
    return bytes(out)


def decode(data):
    """Reference decoder (same arithmetic as source/audio.c). Returns int16 (frames, 2)."""
    rate, frames, block = struct.unpack_from("<III", data, 4)
    nb = (frames + block - 1) // block
    bsz = 8 + block
    rows = []
    preds, idxs, codes = [], [], []
    for b in range(nb):
        o = 16 + b * bsz
        pl, il, _, pr, ir, _ = struct.unpack_from("<hBBhBB", data, o)
        nib = np.frombuffer(data, np.uint8, block, o + 8)
        for ch, (p, ix) in enumerate(((pl, il), (pr, ir))):
            nib = np.frombuffer(data, np.uint8, block // 2, o + 8 + ch * (block // 2))
            c = np.empty(block, np.int32); c[0::2] = nib & 15; c[1::2] = nib >> 4
            preds.append(p); idxs.append(ix); codes.append(c)
    pred = np.array(preds, np.int32); idx = np.array(idxs, np.int32); codes = np.array(codes)
    out = np.zeros(codes.shape, np.int32)
    for i in range(block):
        pred, idx = _step_update(pred, idx, codes[:, i])
        out[:, i] = pred
    out = out.reshape(nb, 2, block).transpose(0, 2, 1).reshape(nb * block, 2)[:frames]
    return out.astype(np.int16)


if __name__ == "__main__":
    cmd, src = sys.argv[1], sys.argv[2]
    pcm = np.frombuffer(open(src, "rb").read(), np.int16).reshape(-1, 2)
    if cmd == "encode":
        open(sys.argv[3], "wb").write(encode(pcm))
    elif cmd == "test":
        import time
        t = time.time(); enc = encode(pcm); te = time.time() - t
        dec = decode(enc)
        err = dec.astype(np.float64) - pcm
        snr = 10 * np.log10((pcm.astype(np.float64) ** 2).mean() / (err ** 2).mean())
        print(f"{len(pcm)} frames: {len(pcm) * 4 / 1e6:.1f} MB -> {len(enc) / 1e6:.1f} MB in {te:.1f}s, SNR {snr:.1f} dB")
