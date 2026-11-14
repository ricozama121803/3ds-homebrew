"""LZ11 (the compression used by Home Menu *_LZ.bin files)."""
import struct


def decompress(d):
    assert d[0] == 0x11, "not LZ11"
    size = d[1] | (d[2] << 8) | (d[3] << 16)
    p = 4
    if size == 0:
        size = struct.unpack_from('<I', d, 4)[0]
        p = 8
    out = bytearray()
    while len(out) < size:
        flags = d[p]
        p += 1
        for i in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> i):
                b = d[p]
                t = b >> 4
                if t == 0:
                    ln = ((b & 15) << 4 | d[p + 1] >> 4) + 0x11
                    disp = ((d[p + 1] & 15) << 8 | d[p + 2]) + 1
                    p += 3
                elif t == 1:
                    ln = ((b & 15) << 12 | d[p + 1] << 4 | d[p + 2] >> 4) + 0x111
                    disp = ((d[p + 2] & 15) << 8 | d[p + 3]) + 1
                    p += 4
                else:
                    ln = t + 1
                    disp = ((b & 15) << 8 | d[p + 1]) + 1
                    p += 2
                for _ in range(ln):
                    out.append(out[-disp])
            else:
                out.append(d[p])
                p += 1
    return bytes(out)


def compress(data):
    """Valid LZ11 using literals only (larger than the original, but always correct)."""
    n = len(data)
    out = bytearray([0x11, n & 255, (n >> 8) & 255, (n >> 16) & 255]) if n < 1 << 24 else \
        bytearray([0x11, 0, 0, 0]) + struct.pack('<I', n)
    for i in range(0, n, 8):
        chunk = data[i:i + 8]
        out.append(0)
        out += chunk
    return bytes(out)
