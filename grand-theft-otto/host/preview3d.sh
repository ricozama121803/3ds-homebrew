#!/usr/bin/env bash
# Renders sample frames of the 3D view on the PC. Usage: host/preview3d.sh [outdir]   (needs gcc and python3 with pillow)
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-/tmp/gta-preview3d}"
mkdir -p "$out"
python3 - "$out" <<'PY'
import struct, sys
from PIL import Image
out = sys.argv[1]
for n in ("world", "sprites"):
    im = Image.open(f"assets/gfx3d/{n}.png").convert("RGBA")
    open(f"{out}/{n}.rgba", "wb").write(struct.pack("<ii", *im.size) + im.tobytes())
PY
cp assets/map.bin "$out/map.bin"
gcc -O2 -DHOST_PREVIEW -Wall -Wno-unused-function -Wno-misleading-indentation -o "$out/preview3d" host/preview3d.c -lm
"$out/preview3d" "$out"
python3 - "$out" <<'PY'
import glob, sys
from PIL import Image
for p in glob.glob(sys.argv[1] + "/s3_*.ppm"):
    Image.open(p).save(p[:-4] + ".png")
PY
echo "frames written to $out"
