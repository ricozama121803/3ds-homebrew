"""Glass-tile restyle for the Home Menu tile frames and cursor (pure PIL)."""
from PIL import Image, ImageFilter


def erode(a, n):
    for _ in range(n):
        a = a.filter(ImageFilter.MinFilter(3))
    return a


def glass_tile(img, rim=2):
    """img is the right half of a tile (flat left edge = mirror seam). Returns restyled RGBA."""
    w, h = img.size
    A = img.getchannel('A')
    pad = 8
    padded = Image.new('L', (w + pad, h))
    padded.paste(A, (pad, 0))
    padded.paste(A.crop((0, 0, 1, h)).resize((pad, h)), (0, 0))  # keep the seam from eroding
    e1 = erode(padded, 1).crop((pad, 0, w + pad, h))
    e3 = erode(padded, 1 + rim).crop((pad, 0, w + pad, h))
    pa, p1, p3, out = A.load(), e1.load(), e3.load(), Image.new('RGBA', (w, h))
    po = out.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        body = 232 - 26 * t / 0.48 if t < 0.48 else 118 + 70 * (t - 0.48) / 0.52
        for x in range(w):
            a = pa[x, y]
            if a == 0:
                continue
            o, r, b = (a - p1[x, y]) / 255, (p1[x, y] - p3[x, y]) / 255, p3[x, y] / 255
            tot = o + r + b or 1
            lum = (o * 28 + r * 255 + b * body) / tot
            po[x, y] = (int(lum),) * 3 + (a,)
    return out


def glow_cursor(img):
    """Bright bracket (original shape) with a soft glow that fades out before the texture edge."""
    w, h = img.size
    A = img.getchannel('A')
    glow = A.filter(ImageFilter.GaussianBlur(2.2))
    pa, pg = A.load(), glow.load()
    alpha = Image.new('L', (w, h))
    po = alpha.load()
    for y in range(h):
        for x in range(w):
            edge = min(x, y, w - 1 - x, h - 1 - y)
            fade = min(1.0, edge / 3.0)
            po[x, y] = max(pa[x, y], int(min(255, pg[x, y] * 1.5) * 0.6 * fade))
    out = Image.new('RGBA', (w, h), (255, 255, 255, 0))
    out.putalpha(alpha)
    return out
