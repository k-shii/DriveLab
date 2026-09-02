#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  echo "Run this installer as root (or with sudo)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/drivelab.cpp"
DEMO_UI_SOURCE="$SCRIPT_DIR/demo_ui.cpp"
DEMO_UI_HEADER="$SCRIPT_DIR/demo_ui.h"

if [[ ! -f "$SOURCE" || ! -f "$DEMO_UI_SOURCE" || ! -f "$DEMO_UI_HEADER" ]]; then
  echo "drivelab.cpp, demo_ui.cpp and demo_ui.h must be in the same directory as this installer." >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This bootstrap installer currently supports Debian/Ubuntu/Proxmox (apt-get)." >&2
  echo "Install a C++17 compiler, ncursesw development headers, fio, smartmontools, hdparm, nwipe, tmux, ioping, sysstat and util-linux manually, then compile drivelab.cpp." >&2
  exit 1
fi

apt-get update
apt-get install -y build-essential libncurses-dev fio smartmontools hdparm nwipe tmux ioping sysstat util-linux

g++ -std=c++17 -O2 -Wall -Wextra "$SOURCE" "$DEMO_UI_SOURCE" -lncursesw -o /usr/local/sbin/drivelab
chmod 0755 /usr/local/sbin/drivelab
mkdir -p /root/drivelab-reports
chmod 0700 /root/drivelab-reports

echo
echo "Installed: /usr/local/sbin/drivelab"
echo "Run:       drivelab"
echo "Reports:   /root/drivelab-reports"
