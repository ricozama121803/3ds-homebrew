"""Decode (and re-encode) BCLIM textures used by the Home Menu layouts."""
import struct
from PIL import Image

FMT = {0: 'L8', 1: 'A8', 2: 'LA4', 3: 'LA8', 5: 'RGB565', 12: 'L4', 13: 'A4'}
BPP = {'L8': 8, 'A8': 8, 'LA4': 8, 'LA8': 16, 'RGB565': 16, 'L4': 4, 'A4': 4}


def pow2(n):
    p = 8
    while p < n:
        p *= 2
    return p


def morton(i):
    x = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4)
    y = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4)
    return x, y


def info(b):
    assert b[-0x28:-0x24] == b'CLIM'
    im = b[-0x14:]
    w, h, f = struct.unpack_from('<HHI', im, 8)
    return w, h, FMT[f & 0xFF], f


def decode(b):
    w, h, fmt, _ = info(b)
    pw, ph = pow2(w), pow2(h)
    bits = BPP[fmt]
    data = b[:-0x28]
    img = Image.new('RGBA', (pw, ph))
    px = img.load()
    pos = 0
    for ty in range(ph // 8):
        for tx in range(pw // 8):
            for i in range(64):
                x, y = morton(i)
                if bits == 16:
                    v = data[pos] | data[pos + 1] << 8; pos += 2
                elif bits == 8:
                    v = data[pos]; pos += 1
                else:
                    byte = data[pos + i // 2]
                    v = (byte >> 4) if (i & 1) else (byte & 15)
                if fmt == 'LA8':
                    a, l = v & 255, v >> 8
                    c = (l, l, l, a)
                elif fmt == 'L8':
                    c = (v, v, v, 255)
                elif fmt == 'A8':
                    c = (255, 255, 255, v)
                elif fmt == 'LA4':
                    l, a = (v >> 4) * 17, (v & 15) * 17
                    c = (l, l, l, a)
                elif fmt == 'L4':
                    c = (v * 17, v * 17, v * 17, 255)
                elif fmt == 'A4':
                    c = (255, 255, 255, v * 17)
                else:  # RGB565
                    c = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
                px[tx * 8 + x, ty * 8 + y] = c
            if bits == 4:
                pos += 32
    return img.crop((0, 0, w, h))


def encode_la8(img, template):
    """Encode an RGBA image (L taken from R, A from alpha) as LA8 using template's footer/size."""
    w, h, fmt, _ = info(template)
    assert fmt == 'LA8' and img.size == (w, h)
    pw, ph = pow2(w), pow2(h)
    px = img.load()
    out = bytearray()
    for ty in range(ph // 8):
        for tx in range(pw // 8):
            for i in range(64):
                x, y = morton(i)
                X, Y = tx * 8 + x, ty * 8 + y
                if X < w and Y < h:
                    r, g, b, a = px[X, Y]
                    out += bytes((a, r))
                else:  # unused padding: keep whatever the original had
                    out += template[len(out):len(out) + 2]
    assert len(out) == len(template) - 0x28
    return bytes(out) + template[-0x28:]
