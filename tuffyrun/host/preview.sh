#!/usr/bin/env bash
# Renders sample frames on the PC. Usage: host/preview.sh <output-dir>   (needs gcc + python3 with pillow/numpy)
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-/tmp/tuffy-preview}"
mkdir -p "$out"
python3 host/preview.py "$out" --raw
gcc -O2 -DHOST_PREVIEW -Wall -Wextra -Wno-unused-function -Wno-misleading-indentation -o "$out/preview" host/preview.c -lm
"$out/preview" "$out"
python3 host/preview.py "$out" > /dev/null
echo "frames written to $out"
