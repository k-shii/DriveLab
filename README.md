# DriveLab

DriveLab is a Linux-first terminal project for guided storage workflows. The
current published version puts a separate Core behind the established terminal
interface before production storage providers are added.

![DriveLab 0.1.0 interface prototype](0.1.0/screenshots/0.1.0-demo3.png)

## Current Published Version

### DriveLab 0.2.0 — Core Architecture

DriveLab 0.2.0 is mostly an under-the-hood update. The interface remains
visually similar to 0.1.0, but drive data, workflows, jobs, events, progress,
ETA, protection decisions, and scheduling now live behind Core application
interfaces.

The release adds CMake, C++20 Core targets, stable physical `DriveId` values,
mock providers, capability discovery, structured results, `--dry-run`, and
seven automated test targets. The TUI now mainly handles ncurses rendering,
layout, focus, input, scrolling, and modal presentation.

This is not yet a production storage utility. Demo and dry-run modes use mock
data and execute no storage commands. Production hardware mode is intentionally
unavailable until real Linux discovery and safety work begins in 0.3.0.

## Build and Run 0.2.0

DriveLab currently targets Linux terminals. On Debian, Ubuntu, or Proxmox:

```bash
sudo apt update
sudo apt install cmake build-essential libncurses-dev

cd 0.2.0/source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

./build/src/drivelab --demo
./build/src/drivelab --dry-run
```

Demo and dry-run modes do not need root and do not touch real drives.

## Versioned Snapshots

Each published version keeps its own source and documentation:

- [0.2.0 — Core Architecture](0.2.0/)
- [0.1.0 — Interface Prototype](0.1.0/)

Future versions will use the same self-contained layout so older milestones
remain easy to browse.

## Current Direction

The next milestone is 0.3.0: real Linux drive discovery, stable device
resolution, ownership detection, and production safety classification. Real
diagnostics, performance tests, persistent jobs, and sanitisation follow in
later stages.

## Documentation

- [0.2.0 manual](0.2.0/MANUAL-0.2.0.md)
- [0.2.0 changelog](0.2.0/CHANGELOG-0.2.0.md)
- [0.2.0 roadmap](0.2.0/ROADMAP-0.2.0.md)
- [0.1.0 archive](0.1.0/)
- [Repository changelog](CHANGELOG.md)
- [Project roadmap](ROADMAP.md)
- [Contributing](CONTRIBUTING.md)

License: pending.
