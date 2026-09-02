#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  echo "Run this installer as root (or with sudo)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${DRIVELAB_BUILD_DIR:-$REPO_ROOT/build}"

if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]]; then
  echo "Run this installer from a complete DriveLab source tree." >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This bootstrap installer currently supports Debian/Ubuntu/Proxmox (apt-get)." >&2
  echo "Install CMake, a C++20 compiler, ncursesw development headers, and desired optional providers manually." >&2
  exit 1
fi

apt-get update
apt-get install -y cmake build-essential libncurses-dev

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure
cmake --install "$BUILD_DIR"
chmod 0755 /usr/local/sbin/drivelab

echo
echo "Installed: /usr/local/sbin/drivelab"
echo "Demo:      drivelab --demo"
echo "Dry run:   drivelab --dry-run"
