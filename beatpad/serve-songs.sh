#!/usr/bin/env bash
# Shares the converted songs in library/ with BeatPad's Get Songs screen until Ctrl+C.
cd "$(dirname "$0")" && exec python3 tools/songs.py serve
