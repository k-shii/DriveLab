DriveLab 0.2 compatibility TUI

Purpose
-------
The retained ncurses frontend hosts the Core-backed DriveLab demo. Demo drives,
workflows, events, jobs, safety decisions, scheduling, and progress come from
the 0.2 Core interfaces. No production storage provider is connected yet.

Install on Proxmox/Debian
-------------------------
apt update
apt install -y cmake build-essential libncurses-dev

Build
-----
From the repository root:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

Run
---
Demo mode (no root, storage commands, device I/O, or report writes):
./build/src/drivelab --demo

Core dry-run bootstrap (mock inventory and no process/device execution):
./build/src/drivelab --dry-run

Production hardware mode is not available in DriveLab 0.2.0. Running without
an option exits safely and directs the user to --demo or --dry-run.

Temporary mouse verification output is written to
/tmp/drivelab-demo-mouse.log.

Demo controls
-------------
Mouse    select feature options; activate explicit controls on matched release
Wheel    scroll the drive, selected-tab, or bottom viewport under the pointer
Tab      move forward in the current input level (Shift+Tab moves backward)
Up/Down move through drive or feature options without mouse input
Left/Right switch feature tabs or modal choices
Enter    open the selected feature workflow / activate / move inward
Esc      cancel / move outward
O/S/A/B/N select Overview / S.M.A.R.T / ATA-HPA / Benchmark / Sanitize
E/J      show Event Log / Job Queue
PgUp/PgDn scroll the active bottom pane
q        open the Cancel-default detach confirmation
Shift+Q  request demo session termination

The header shows the current Core version and provides keyboard/mouse-accessible
Settings and Detach UI controls. Settings remains a placeholder in 0.2.
