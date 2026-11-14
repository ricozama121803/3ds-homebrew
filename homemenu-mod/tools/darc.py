"""Minimal DARC reader: list files and give (offset, size) so they can be patched in place."""
import struct


def entries(d):
    assert d[:4] == b'darc'
    tbl, tlen, doff = struct.unpack_from('<III', d, 0x10)
    cnt = struct.unpack_from('<I', d, tbl + 8)[0]
    nm = tbl + cnt * 12
    nodes = [struct.unpack_from('<III', d, tbl + i * 12) for i in range(cnt)]

    def name(i):
        e = nm + (nodes[i][0] & 0xFFFFFF)
        j = e
        while d[j:j + 2] != b'\0\0' or (j - e) % 2:
            j += 1
        return d[e:j].decode('utf-16le')

    out = {}
    stack = [(0, nodes[0][2])]
    path = []
    for i in range(1, cnt):
        while len(stack) > 1 and i >= stack[-1][1]:
            stack.pop()
            path.pop()
        n = name(i)
        if nodes[i][0] >> 24:
            stack.append((i, nodes[i][2]))
            path.append(n)
        else:
            out['/'.join([p for p in path if p != '.'] + [n])] = (nodes[i][1], nodes[i][2])
    return out
