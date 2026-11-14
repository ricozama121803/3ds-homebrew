#!/usr/bin/env bash
# Renders sample frames on the PC. Usage: host/preview.sh <output-dir> [song library]   (needs gcc + python3 with pillow)
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-/tmp/beatpad-preview}"
lib="$(realpath "${2:-library}")"             # songs to draw (the first one in the list)
mkdir -p "$out"
root="$(mktemp -d)"; trap 'rm -rf "$root"' EXIT
ln -s "$lib" "$root/songs"
gcc -O2 -DHOST_PREVIEW -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -o "$out/preview" host/preview.c -lm -lpthread
"$out/preview" "$out" "$root"
python3 host/preview.py "$out" > /dev/null
echo "frames written to $out"
