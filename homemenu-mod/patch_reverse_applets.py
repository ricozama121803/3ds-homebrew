"""Test mod: reverse the order of the applet button row on the Home Menu's bottom screen."""
import os, struct, sys
sys.path.insert(0, 'tools')
import lz11, darc, bclyt

SRC = os.path.expanduser('~/backups/3ds-nand/homemenu-romfs/launcher_LZ.bin')
TID = '0004003000008F02'
OUT = f'out/luma/titles/{TID}/romfs/launcher_LZ.bin'
LAYOUT = 'blyt/LncBase_D_01.bclyt'

d = bytearray(lz11.decompress(open(SRC, 'rb').read()))
off, size = darc.entries(bytes(d))[LAYOUT]
lay = bytes(d[off:off + size])
by_name = {name: (p, tx) for p, _, name, tx, ty, w, h in bclyt.panes(lay)}

# Swap x positions: Memo<->Mvs, Frd<->Web (News stays in the middle). Cursor anchors move too.
for a, b in (('Memo', 'Mvs'), ('Frd', 'Web')):
    for prefix in ('N_', 'N_CPos_'):
        na, nb = f'{prefix}{a}_00', f'{prefix}{b}_00'
        (pa, xa), (pb, xb) = by_name[na], by_name[nb]
        struct.pack_into('<f', d, off + pa + 36, xb)
        struct.pack_into('<f', d, off + pb + 36, xa)
        print(f'{na}: {xa} -> {xb}   {nb}: {xb} -> {xa}')

# Notification lamps sit a fixed offset to the right of their button: move them with it.
OLD = {k: by_name[f'N_{k}_00'][1] for k in ('Frd', 'News', 'Web', 'Mvs')}
NEW = {'Frd': OLD['Web'], 'News': OLD['News'], 'Web': OLD['Frd'], 'Mvs': OLD['Mvs'] - 168}
for k in OLD:
    pl, xl = by_name[f'N_{k}Rcv_00']
    nx = xl - OLD[k] + NEW[k]
    struct.pack_into('<f', d, off + pl + 36, nx)
    print(f'N_{k}Rcv_00: {xl} -> {nx}')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, 'wb').write(lz11.compress(bytes(d)))

# Verify by reading the result back.
back = lz11.decompress(open(OUT, 'rb').read())
assert len(back) == len(d)
o2, s2 = darc.entries(back)[LAYOUT]
for p, _, name, tx, ty, w, h in bclyt.panes(back[o2:o2 + s2]):
    if name.startswith(('N_Memo', 'N_Frd', 'N_News', 'N_Web', 'N_Mvs', 'N_CPos_')):
        print(f'  {name:18s} x={tx}')
print('wrote', OUT, os.path.getsize(OUT), 'bytes')
