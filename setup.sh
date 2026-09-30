#!/usr/bin/env bash
# One-time root setup for devkitPro. Run:  bash /home/ricozama/projects/3ds/setup.sh
# Installs the .deb with dpkg directly (apt is blocked by the broken jellyfin-media-player package).
set -euo pipefail
cd "$(dirname "$0")/tools"

# libarchive-tools (bsdtar) is only needed by makepkg, not by dkp-pacman itself, so skip that dependency
sudo dpkg -i --ignore-depends=libarchive-tools devkitpro-pacman.deb
sudo dkp-pacman -S --noconfirm 3ds-dev

echo
echo "Done. Next: source ~/projects/3ds/env.sh && cd ~/projects/3ds/hello3ds && make"
