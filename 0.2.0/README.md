# DriveLab 0.2.0 — Core Architecture

DriveLab 0.2.0 is mostly an under-the-hood release. The terminal interface
still looks and behaves much like 0.1.0, but the drive model, workflows, jobs,
events, progress, protection decisions, and scheduling now live behind a
separate Core.

This is still a simulated storage workbench rather than a production storage
utility. Real Linux drive discovery starts in 0.3.0.

## What Changed

- Added a CMake/C++20 project with a standalone, ncurses-independent Core
  target.
- Added structured result, error, capability, operation, configuration, and
  report contracts.
- Added focused inventory, health, ATA, benchmark, and sanitize provider
  interfaces with mock implementations.
- Added stable `DriveId` values derived from hardware identity rather than
  treating `/dev/sdX` as physical identity.
- Moved the complete approved demo model into Core, including all seven mock
  drives and their Overview, S.M.A.R.T., ATA/HPA, and benchmark data.
- Moved simulated workflow submission, events, jobs, progress, ETA, controls,
  protection checks, and scheduling into Core.
- Added `--dry-run`, which reports mock capabilities and inventory while
  keeping external process execution disabled.
- Added seven automated CTest targets covering the Core boundaries and demo
  behavior.
- Removed the legacy frontend-owned live hardware path.

The TUI now mainly owns ncurses rendering, layout, focus, keyboard and mouse
input, scrolling, and modal presentation.

## Current Scope

`--demo` provides the approved interactive interface using canned drives and
simulated workflows. A fresh session starts with an empty Job Queue. Jobs are
created only after a workflow is explicitly confirmed.

`--dry-run` currently uses the same mock inventory and reports what the 0.2
Core can represent. It does not inspect the host or execute a storage command.

Production hardware mode is intentionally unavailable. Running `drivelab`
without a mode exits safely with:

```text
Production hardware mode is not available in DriveLab 0.2.0. Use --demo or --dry-run.
```

## Build and Test

On Debian, Ubuntu, or Proxmox:

```bash
sudo apt update
sudo apt install cmake build-essential libncurses-dev

cd 0.2.0/source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/src/drivelab --demo
./build/src/drivelab --dry-run
./build/src/drivelab --help
./build/src/drivelab --version
```

Demo and dry-run modes do not require root and do not access real drives.

## Version Files

- [Manual](MANUAL-0.2.0.md)
- [Changelog](CHANGELOG-0.2.0.md)
- [Roadmap at 0.2.0](ROADMAP-0.2.0.md)
- [Source](source/)

License: pending.
