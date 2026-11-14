#!/usr/bin/env bash
# Shares library/ with BeatPad's Get Songs screen and runs Song Studio at http://localhost:47330 (record a browser
# tab or drop files to make new songs). Builds Song Studio the first time. Ctrl+C to stop.
set -e
cd "$(dirname "$0")"
if [ ! -f studio/out/index.html ]; then
	if command -v npm >/dev/null; then
		echo "Building Song Studio (first run only)..."
		(cd studio && npm install --no-audit --no-fund --silent && npm run build --silent >/dev/null)
	else
		echo "Song Studio needs Node.js to build once (https://nodejs.org); the 3DS song sharing works without it."
	fi
fi
exec python3 tools/songs.py serve
