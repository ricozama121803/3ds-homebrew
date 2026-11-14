#!/usr/bin/env bash
# Builds a project inside the official devkitARM container (podman or docker), for machines without a local
# devkitPro install.  Usage: ./dkp-make.sh <project dir> [make args...]
# With no make args it builds the .3dsx in the container, then the .cia on the host (the prebuilt makerom /
# bannertool in tools/bin need a newer glibc than the container has).
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
proj="$1"; shift
engine="$(command -v podman || command -v docker)"
run() { "$engine" run --rm -v "$root:/work:Z" -w "/work/$proj" docker.io/devkitpro/devkitarm:latest make "$@"; }
if [ $# -gt 0 ]; then run "$@"; exit; fi
run 3dsx
(cd "$root/$proj" && env -u DEVKITARM make cia)
