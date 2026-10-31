#!/usr/bin/env bash
# Installs the mkPolarisEdge build from build-system/ system-wide (needs root).
# Usage: ./install.sh
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-system"

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "No build found - running ./build.sh first."
    "$SRC_DIR/build.sh"
else
    # rebuild changed sources as the normal user, so no root-owned files end up in build-system/
    cmake --build "$BUILD_DIR" --parallel "$(nproc)"
fi

sudo cmake --install "$BUILD_DIR"
# list of installed files, used for uninstalling: sudo xargs rm < build-system/install_manifest.txt
sudo chown "$(id -u):$(id -g)" "$BUILD_DIR/install_manifest.txt"

echo
echo "Installed. Restart KWin so it loads the new plugin:"
echo "  Wayland: log out and back in"
echo "  X11:     kwin_x11 --replace &"
echo "Then choose \"mkPolarisEdge\" under System Settings > Colors & Themes > Window Decorations."
