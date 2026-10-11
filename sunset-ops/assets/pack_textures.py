#!/usr/bin/env python3
"""Packs .t3x files into one blob: u32 count, count x (u32 offset, u32 size), then the files (16-byte aligned). Usage: pack_textures.py out.bin a.t3x b.t3x ..."""
import struct, sys
out, files = sys.argv[1], sys.argv[2:]
blobs = [open(f, "rb").read() for f in files]
off = 4 + 8 * len(blobs)
off = (off + 15) & ~15
table, body = [], b""
for b in blobs:
    table.append((off + len(body), len(b)))
    body += b + b"\0" * ((-len(b)) % 16)
hdr = struct.pack("<I", len(blobs)) + b"".join(struct.pack("<II", o, s) for o, s in table)
hdr += b"\0" * (off - len(hdr))
open(out, "wb").write(hdr + body)
