#!/usr/bin/env bash
# Renders sample frames of the 3D view on the PC. Usage: host/preview.sh [outdir]   (needs gcc and python3 with pillow)
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-/tmp/webslinger-preview}"
mkdir -p "$out"
python3 - "$out" <<'PY'
import struct, sys
from PIL import Image
out = sys.argv[1]
for n in "white road glass cream red tan copper roof grass water hero glow cloud".split():
    im = Image.open(f"assets/gfx/{n}.png").convert("RGBA")
    open(f"{out}/{n}.rgba", "wb").write(struct.pack("<ii", *im.size) + im.tobytes())
PY
gcc -O2 -DHOST_PREVIEW -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-unused-variable -o "$out/preview" host/preview.c -lm
"$out/preview" "$out" "$out"
python3 host/preview.py "$out"
echo "frames written to $out"
