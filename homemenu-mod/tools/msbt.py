"""Read and rewrite Home Menu MSBT string tables (UTF-16LE, label -> string)."""
import struct


def sections(d):
    n = struct.unpack_from('<H', d, 0x0E)[0]
    p, out = 0x20, {}
    for _ in range(n):
        mg = d[p:p + 4]
        sz = struct.unpack_from('<I', d, p + 4)[0]
        out[mg] = (p, sz)
        p += (0x10 + sz + 15) & ~15
    return out


def labels(d, secs):
    lp, _ = secs[b'LBL1']
    q = lp + 0x10
    groups = struct.unpack_from('<I', d, q)[0]
    res = {}
    for i in range(groups):
        c, off = struct.unpack_from('<II', d, q + 4 + 8 * i)
        r = lp + 0x10 + off
        for _ in range(c):
            ln = d[r]
            name = d[r + 1:r + 1 + ln].decode()
            res[name] = struct.unpack_from('<I', d, r + 1 + ln)[0]
            r += 5 + ln
    return res


def strings(d, secs):
    tp, ts = secs[b'TXT2']
    base = tp + 0x10
    cnt = struct.unpack_from('<I', d, base)[0]
    offs = [struct.unpack_from('<I', d, base + 4 + 4 * i)[0] for i in range(cnt)]
    ends = offs[1:] + [ts]
    return [d[base + o:base + e] for o, e in zip(offs, ends)]


def replace(d, edits):
    """edits: {label: new text}. Returns new file bytes (TXT2 rebuilt, sizes fixed)."""
    secs = sections(d)
    lab = labels(d, secs)
    strs = strings(d, secs)
    for name, text in edits.items():
        strs[lab[name]] = text.encode('utf-16le') + b'\0\0'
    cnt = len(strs)
    table = 4 + 4 * cnt
    offs, pos = [], table
    for s in strs:
        offs.append(pos)
        pos += len(s)
    body = struct.pack('<I', cnt) + b''.join(struct.pack('<I', o) for o in offs) + b''.join(strs)
    tp, ts = secs[b'TXT2']
    old_end = tp + ((0x10 + ts + 15) & ~15)
    new_sec = bytearray(d[tp:tp + 0x10]) + body
    struct.pack_into('<I', new_sec, 4, len(body))
    new_sec += b'\xab' * ((-len(new_sec)) % 16)
    out = bytearray(d[:tp]) + new_sec + d[old_end:]
    struct.pack_into('<I', out, 0x12, len(out))
    return bytes(out)
