#!/usr/bin/env bash
# Renders sample frames on the PC. Usage: host/preview.sh <output-dir>   (needs gcc + python3 with pillow)
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-/tmp/beatpad-preview}"
mkdir -p "$out"
gcc -O2 -DHOST_PREVIEW -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o "$out/preview" host/preview.c -lm -lpthread
"$out/preview" "$out"
python3 host/preview.py "$out" > /dev/null
echo "frames written to $out"
