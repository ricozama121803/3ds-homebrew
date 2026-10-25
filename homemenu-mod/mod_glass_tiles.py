"""Glass tiles + glowing cursor: restyle the Home Menu's icon frames and selection cursor."""
import os, sys
sys.path.insert(0, 'tools'); sys.path.insert(0, '.')
import lz11, darc, bclim, style

SRC = os.path.expanduser('~/backups/3ds-nand/homemenu-romfs/launcher_LZ.bin')
TID = '0004003000008F02'
OUT = f'out/luma/titles/{TID}/romfs/launcher_LZ.bin'
TILES = {'LncIcon_11': 2, 'LncIcon_13': 2, 'LncIcon_14': 1, 'LncIcon_15': 1, 'LncIcon_16': 1}

orig = lz11.decompress(open(SRC, 'rb').read())
d = bytearray(orig)
E = darc.entries(orig)


def patch(name, make):
    off, size = E[f'timg/{name}.bclim']
    old = bytes(d[off:off + size])
    assert bclim.encode_la8(bclim.decode(old), old) == old, f'encoder mismatch on {name}'
    new = bclim.encode_la8(make(bclim.decode(old)), old)
    assert len(new) == len(old)
    d[off:off + size] = new
    print('patched', name, size, 'bytes')


for name, rim in TILES.items():
    patch(name, lambda im, r=rim: style.glass_tile(im, r))
patch('LncCsr_41', style.glow_cursor)

assert len(d) == len(orig)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, 'wb').write(lz11.compress(bytes(d)))
assert lz11.decompress(open(OUT, 'rb').read()) == bytes(d)
changed = sum(a != b for a, b in zip(orig, d))
print('wrote', OUT, os.path.getsize(OUT), 'bytes;', changed, 'bytes differ from original')
