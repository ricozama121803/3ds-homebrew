"""Dump panes (name, type, translation, size) from a BCLYT layout."""
import struct, sys


def panes(b):
    assert b[:4] == b'CLYT'
    hdr, nsec = struct.unpack_from('<H', b, 6)[0], struct.unpack_from('<H', b, 0x10)[0]
    p = hdr
    out = []
    for _ in range(nsec):
        mg = b[p:p + 4].decode('latin1')
        sz = struct.unpack_from('<I', b, p + 4)[0]
        if mg in ('pan1', 'pic1', 'txt1', 'wnd1', 'bnd1'):
            flags, origin, alpha, _, name = struct.unpack_from('<BBBB24s', b, p + 8)
            name = name.split(b'\0')[0].decode('latin1')
            tx, ty, tz, rx, ry, rz, sx, sy, w, h = struct.unpack_from('<10f', b, p + 8 + 28)
            out.append((p, mg, name, tx, ty, w, h))
        p += sz
    return out


if __name__ == '__main__':
    for p, mg, name, tx, ty, w, h in panes(open(sys.argv[1], 'rb').read()):
        print(f"{mg} {name:24s} x={tx:8.1f} y={ty:8.1f} w={w:7.1f} h={h:7.1f}  @0x{p:x}")
