#!/usr/bin/env bash
# Renders sample frames on the PC. Usage: host/preview.sh [outdir] [hi]   (needs gcc, plus tools/venv with pillow)
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-/tmp/ringside-preview}"
mkdir -p "$out"
gcc -O2 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers -o "$out/preview" \
    host/preview.c host/gfx_host.c source/game.c source/ai.c source/fighter.c source/fighter_art.c source/scene.c source/mesh.c -lm
"$out/preview" "$out" "${2:-}" "${3:-1}"
../tools/venv/bin/python - "$out" <<'EOF'
import sys, glob, os
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
print("frames written to", sys.argv[1])
EOF
