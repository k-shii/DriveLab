# DriveLab Roadmap

DriveLab is being built in small versioned stages. The interface came first.
The next work is putting clear, testable boundaries behind it before enabling
production storage operations.

## 0.1.0 — Interface Prototype

- Define the terminal layout, navigation, storage views, and workflow shape.
- Demonstrate protected devices, contextual help, events, jobs, and operation
  choices with mock data.
- Keep all published workflows simulated.

## 0.2.x — Core Architecture

- Separate Core, TUI, providers, jobs, safety, process execution,
  configuration, logging, and reporting.
- Move demo state behind Core interfaces.
- Add stable physical-drive identity, capability discovery, dry-run behavior,
  mock providers, and automated tests.

## 0.3.x — Discovery and Safety

- Add production Linux inventory and stable device resolution.
- Detect mounts, system disks, swap, LVM, RAID, ZFS, and VM ownership.
- Classify devices before exposing actions.

## 0.4.x to 0.7.x — Storage Workflows

- Add health diagnostics and S.M.A.R.T. self-tests.
- Add read-only performance and latency diagnostics.
- Add supervised persistent jobs and reconnectable sessions.
- Add sanitisation only after target checks and destructive-operation policy
  are ready.

## 0.8.x to 1.0.0 — Distribution and Stabilisation

- Add reports, capability checks, configuration, and conventional packaging.
- Audit safety, interruption handling, terminal behavior, and Linux
  compatibility.
- Reach a stable Linux terminal release without making specialist tools part
  of the minimum install.

For now, distribution remains source clone, pull, build, and run.
