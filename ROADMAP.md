# DriveLab Roadmap

DriveLab is being built in small versioned stages. The interface came first,
followed by the Core boundaries beneath it. The next work is real Linux drive
discovery and safety classification.

## 0.2.0 — Core Architecture ![COMPLETED](https://img.shields.io/badge/COMPLETED-brightgreen)

- ☑ Separate Core, TUI, providers, jobs, safety, process execution, configuration, logging, and reporting.
- ☑ Move demo state and simulated workflows behind Core/application interfaces.
- ☑ Use stable physical-drive identity for Core ownership and scheduling.
- ☑ Add capability discovery.
- ☑ Add mock providers.
- ☑ Add dry-run behavior that cannot execute external storage commands.
- ☑ Add automated Core and architecture-boundary tests.
- ☑ Retire the legacy frontend-owned live hardware path.
- ☑ Keep production hardware mode unavailable until discovery and safety are ready.

## 0.3.0 — Discovery and Safety

- ☐ Add production Linux inventory and stable device resolution.
- ☐ Detect mounts, system disks, swap, LVM, RAID, ZFS, and VM ownership.
- ☐ Classify devices before exposing actions.

## 0.4.x — Health Diagnostics

- ☐ Add health diagnostics and S.M.A.R.T. self-tests.

## 0.5.x — Performance Diagnostics

- ☐ Add read-only performance and latency diagnostics.

## 0.6.x — Persistent Jobs

- ☐ Add supervised persistent jobs and reconnectable sessions.

## 0.7.x — Sanitization and Destructive Testing

- ☐ Add sanitisation only after target checks and destructive-operation policy are ready.

## 0.8.x to 1.0.0 — Distribution and Stabilisation

- ☐ Add reports, capability checks, configuration, and conventional packaging.
- ☐ Audit safety, interruption handling, terminal behavior, and Linux compatibility.
- ☐ Reach a stable Linux terminal release without making specialist tools part of the minimum install.

For now, distribution remains source clone, pull, build, and run.

## Completed

### 0.1.0 — Interface Prototype

- ☑ Define the terminal layout, navigation, storage views, and workflow shape.
- ☑ Demonstrate protected devices, contextual help, events, jobs, and operation choices with mock data.
- ☑ Keep all published workflows simulated.
