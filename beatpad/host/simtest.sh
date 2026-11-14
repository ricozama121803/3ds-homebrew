#!/usr/bin/env bash
# Builds and runs the logic tests against every song in library/ (or the folder given).
set -euo pipefail
cd "$(dirname "$0")/.."
lib="$(realpath "${1:-library}")"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
ln -s "$lib" "$tmp/songs"                      # the game looks in <root>/songs
gcc -O2 -Wall -Wextra -o "$tmp/simtest" host/simtest.c source/game.c source/song.c -lm
"$tmp/simtest" "$tmp"
