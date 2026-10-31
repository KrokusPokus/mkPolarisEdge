#!/usr/bin/env bash
# Builds mkPolarisEdge for system-wide installation (prefix /usr) in build-system/.
# Usage: ./build.sh [--clean]
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-system"

# --clean, or a cache from a different source path (e.g. after renaming the folder): start fresh
if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR"
elif [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && \
     ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$SRC_DIR" "$BUILD_DIR/CMakeCache.txt"; then
    echo "Build directory belongs to a different source path - recreating it."
    rm -rf "$BUILD_DIR"
fi

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo
echo "Build finished. Install with: ./install.sh"
