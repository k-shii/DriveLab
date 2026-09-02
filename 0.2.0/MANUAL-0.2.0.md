# DriveLab 0.2.0 — Core Architecture Manual

DriveLab 0.2.0 keeps the approved terminal interface while moving the demo's
storage model and simulated workflow behavior into Core. It is intended for
safe interface, architecture, and workflow testing before real Linux hardware
support is added.

## Requirements

The current build targets Linux and requires:

- CMake 3.16 or newer;
- a C++20 compiler;
- ncursesw development headers;
- POSIX threads.

On Debian, Ubuntu, or Proxmox:

```bash
sudo apt update
sudo apt install cmake build-essential libncurses-dev
```

## Build and Test

From the published source directory:

```bash
cd 0.2.0/source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The final 0.2.0 source was verified with all seven CTest targets passing.

For a Core-only build without the ncurses executable:

```bash
cmake -S . -B build-core -DDRIVELAB_BUILD_TUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core -j"$(nproc)"
ctest --test-dir build-core --output-on-failure
```

An optional Debian/Ubuntu/Proxmox bootstrap installer is included. It installs
the build dependencies, builds and tests DriveLab, and installs the executable
under `/usr/local/sbin`:

```bash
sudo bash prototype/install-drivelab.sh
```

Root is needed by that installer for package and system installation. It is
not needed to run demo or dry-run mode from a local build.

## Launch Modes

### Demo

```bash
./build/src/drivelab --demo
```

Starts the Core-backed ncurses interface with seven canned drives. No storage
utility, device command, report write, or real-drive access occurs.

The session starts with one informational event and no jobs. Confirming a
workflow creates a simulated Core-owned job or records a simulated inspection,
depending on the selected workflow.

### Dry Run

```bash
./build/src/drivelab --dry-run
```

Prints the current Core version, mock capability catalog, canned inventory,
drive states, and stable fixture identities. External process execution stays
disabled. In 0.2.0, dry-run does not inspect the real host.

### Help and Version

```bash
./build/src/drivelab --help
./build/src/drivelab --version
```

### Production Mode

Running without `--demo` or `--dry-run` does not fall back to the old live
prototype code:

```bash
./build/src/drivelab
```

It exits with a message that production hardware mode is unavailable in
DriveLab 0.2.0. Real discovery and production safety work begin in 0.3.0.

## Screen Layout

The interface keeps the established four-part layout:

- a persistent header with the DriveLab version, session status, Settings, and
  Detach UI controls;
- a left drive list with up to five visible entries and a scrollbar;
- a selected-drive area with identity, status, feature tabs, and contextual
  content;
- a fixed lower panel for Event Log and Job Queue.

The minimum terminal size is 70 columns by 24 rows. The layout responds to
terminal resizing and abbreviates secondary labels when space is limited.

## Demo Inventory

The Core-owned fixture contains seven drives:

- two ready HDD examples;
- a protected system disk;
- a protected VM-assigned SSD;
- a protected ZFS member;
- a failing NVMe example;
- a degraded HDD example.

Each drive has a stable `DriveId`. The displayed `/dev` path is a current
observation and is not used as physical identity or as the scheduler lock key.

Protected drives remain visible. They expose identity and cached status data,
while actionable workflows are unavailable. Core enforces this rule even if a
frontend attempts to submit a protected workflow directly.

## Feature Views

### Overview

Shows mock identity, transport, temperature, power-on hours, health counters,
ATA security, and HPA state.

### S.M.A.R.T.

Shows mock health data and simulated Short, Extended, Conveyance, and Raw Data
workflow choices. Self-tests become simulated jobs; Raw Data is an immediate
simulated inspection.

### ATA / HPA

Shows mock ATA security, frozen state, HPA capacity, DCO status, erase support,
and estimated durations. Inspection and erase workflow choices remain
simulated.

### Benchmark

Shows Quick Sequential, HDD Zone Characterization, Random 4K, and latency
profiles with representative mock results. Confirmed profiles create simulated
jobs.

### Sanitize

Shows the recommended and alternative mock sanitization methods, their safety
classification, contextual explanation, and estimated duration where known.
Every destructive workflow is clearly marked and remains simulation-only.

Clicking or focusing a feature option selects it and updates the contextual
panel. It does not submit work. Press `Enter` or activate `Open Workflow` to
open the next screen, then explicitly confirm the simulation.

## Jobs and Events

The Event Log and Job Queue are projections of Core state. The lower panel
shows up to five entries at once and supports scrolling.

The demo scheduler applies these rules:

- one active job may own a physical `DriveId` at a time;
- another job for the same `DriveId` remains queued;
- jobs for different `DriveId` values may progress concurrently;
- a paused job retains its physical-drive lock;
- completing, failing, or cancelling an active cancellable job releases its
  lock and starts the next eligible queued job;
- non-cancellable simulated firmware erase jobs can block session termination.

Progress, ETA, completion, failure, pause, resume, cancellation, and queue
handoff are simulated by Core. They do not represent real device activity.
All state is in memory and is discarded when the standalone process exits.

## Keyboard Controls

| Key | Action |
| --- | --- |
| `Tab` / `Shift+Tab` | Move between controls at the current level |
| `Up` / `Down` | Move through drives or feature options |
| `Left` / `Right` | Change feature tabs or modal choices |
| `Enter` | Select, open, confirm, or move inward |
| `Esc` | Cancel or move outward |
| `R` | Run a simulated inventory scan |
| `O` | Open Overview |
| `S` | Open S.M.A.R.T. |
| `A` | Open ATA / HPA |
| `B` | Open Benchmark |
| `N` | Open Sanitize |
| `E` | Show Event Log |
| `J` | Show Job Queue |
| `Page Up` / `Page Down` | Scroll the active lower view |
| `q` | Open the detach confirmation |
| `Shift+Q` | Open the terminate-session confirmation |

The program starts with focus on Scan. Entering a drive activates its feature
tabs. `Esc` moves outward from a workflow dialog to its feature panel, from the
panel to the feature tabs, and from the tabs to the drive list.

The Settings control remains a placeholder in 0.2.0.

## Mouse Controls

Mouse clicks can select drives, tabs, options, buttons, and available job
controls. Feature-option clicks select only; explicit workflow controls move
deeper or confirm an action.

A control activates only when the left-button release resolves to the same
region recorded on press. Hover can update secondary highlighting and
contextual help without replacing keyboard focus. The wheel scrolls only the
drive, feature, or lower viewport under the pointer.

Some terminals do not provide mouse-motion events. Clicks and keyboard-only
navigation remain available in that case.

Temporary mouse diagnostics are written to:

```text
/tmp/drivelab-demo-mouse.log
```

## Current Boundaries

DriveLab 0.2.0 does not provide:

- real Linux drive discovery or ownership classification;
- production S.M.A.R.T., ATA, benchmark, latency, or sanitization providers;
- production job persistence or tmux supervision;
- production destructive-operation confirmation and enforcement;
- filesystem report output or user-loaded configuration.

The session and detach labels still demonstrate the intended persistent
workflow. There is no persistent controller in 0.2.0, so exiting the standalone
program ends the demo session and discards its in-memory jobs.

## Common Problems

- **The interface is clipped:** enlarge the terminal to at least 70x24.
- **CMake cannot find curses:** install the distribution's ncurses development
  package.
- **Mouse hover is missing:** the terminal may not report mouse motion; use
  clicks or keyboard navigation.
- **Bare launch reports unavailable:** this is expected in 0.2.0; use `--demo`
  or `--dry-run`.
