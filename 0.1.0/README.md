# DriveLab 0.1.0 — Interface Prototype

DriveLab 0.1.0 is a functional interface/workflow prototype, not yet a
functional storage utility.

This milestone establishes how DriveLab looks and behaves in a terminal. It
demonstrates the TUI, navigation, drive views, protected-drive states,
contextual help, Event Log, Job Queue, and simulated/mock workflows.

## What Works in 0.1.0

- Responsive ncurses interface with keyboard and mouse navigation.
- Seven canned HDD, SSD, and NVMe examples.
- Ready, protected, degraded, and failing drive presentations.
- Overview, S.M.A.R.T., ATA/HPA, Benchmark, and Sanitize views.
- Contextual explanations for each operation choice.
- Explicit workflow confirmation before a simulation starts.
- Simulated job progress, queueing, pause, resume, stop, and cancellation.
- Five-row drive, event, and job viewports with scrolling.

All of this runs through `--demo`. It does not need root, execute a storage
command, or access a real drive.

## What Is Not in 0.1.0

This version does not provide production hardware discovery, diagnostics,
benchmarks, sanitisation, persistent jobs, or safety enforcement. The source
contains early exploratory Linux integration code, preserved as part of the
historical snapshot, but it is not a supported production storage workflow.

## Build

On Debian, Ubuntu, or Proxmox:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev

cd 0.1.0/source
g++ -std=c++17 -O0 -g -Wall -Wextra *.cpp -lncursesw -pthread -o drivelab
```

## Run

```bash
./drivelab --demo
```

Demo mode writes temporary mouse diagnostics to
`/tmp/drivelab-demo-mouse.log`. It does not perform device I/O.

## Version Files

- [Manual](MANUAL-0.1.0.md)
- [Changelog](CHANGELOG-0.1.0.md)
- [Roadmap at 0.1.0](ROADMAP-0.1.0.md)
- [Source](source/)

License: pending.
