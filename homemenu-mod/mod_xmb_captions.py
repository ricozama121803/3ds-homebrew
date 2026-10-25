"""Rename the Home Menu's top-screen captions to PS3 XMB wording."""
import os, sys
sys.path.insert(0, 'tools')
import lz11, msbt

SRC = os.path.expanduser('~/backups/3ds-nand/homemenu-romfs/message/US_English/menu_msbt_LZ.bin')
TID = '0004003000008F02'
OUT = f'out/luma/titles/{TID}/romfs/message/US_English/menu_msbt_LZ.bin'
EDITS = {
    'lau_title_menu': 'XrossMediaBar',
    'lau_title_memo': 'Memo Pad',
    'lau_title_fri': 'Friends',
    'lau_title_news': 'Information Board',
    'lau_title_manu': 'Manual',
    'lau_title_memo_u': 'Memo Pad',
    'lau_title_fri_u': 'Friends',
    'lau_title_news_u': 'Information Board',
    'lau_2b_start_news': 'Information Board',
}

d = lz11.decompress(open(SRC, 'rb').read())
new = msbt.replace(d, EDITS)

# Verify: only the edited labels changed, everything else is identical.
a_s = msbt.sections(d); b_s = msbt.sections(new)
assert msbt.labels(d, a_s) == msbt.labels(new, b_s)
la, lb = msbt.labels(d, a_s), msbt.labels(new, b_s)
sa, sb = msbt.strings(d, a_s), msbt.strings(new, b_s)
assert len(sa) == len(sb)
for name, i in la.items():
    same = sa[i] == sb[i]
    assert same == (name not in EDITS), name
    if not same:
        print(f"{name}: {sa[i].decode('utf-16le').strip(chr(0))!r} -> {sb[i].decode('utf-16le').strip(chr(0))!r}")
assert new[:8] == d[:8] and msbt.sections(new)[b'TSY1'][1] == a_s[b'TSY1'][1]

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, 'wb').write(lz11.compress(new))
assert lz11.decompress(open(OUT, 'rb').read()) == new
print('wrote', OUT, os.path.getsize(OUT), 'bytes')
