#!/usr/bin/env bash
# Converts songs for BeatPad and shares them over Wi-Fi.
#   ./add-song.sh song.mp3 [more.mp3 ...]      (or a folder of them)
# Then on the 3DS: BeatPad -> Y (Get Songs). Ctrl+C here when you're done.
set -euo pipefail
cd "$(dirname "$0")"
files=()
for f in "$@"; do
	if [ -d "$f" ]; then while IFS= read -r -d '' x; do files+=("$x"); done < <(find "$f" -maxdepth 1 -type f \( -iname '*.mp3' -o -iname '*.m4a' -o -iname '*.flac' -o -iname '*.wav' -o -iname '*.ogg' -o -iname '*.opus' \) -print0 | sort -z)
	else files+=("$f"); fi
done
[ ${#files[@]} -gt 0 ] || { echo "usage: ./add-song.sh song.mp3 [more ...]   (files or folders)"; exit 1; }
python3 tools/songs.py add "${files[@]}"
echo
exec python3 tools/songs.py serve
