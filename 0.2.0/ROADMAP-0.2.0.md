# DriveLab Roadmap at 0.2.0

This file preserves the public project direction at the 0.2.0 milestone.

## Completed: 0.1.0 — Interface Prototype

- Established the terminal layout and keyboard/mouse interaction model.
- Demonstrated seven mock drives, protected-device presentation, contextual
  feature views, Event Log, Job Queue, and simulated workflows.
- Kept the documented workflow free of real hardware access.

## Completed: 0.2.0 — Core Architecture

- Separated Core, TUI, providers, jobs, safety, process execution,
  configuration, logging, and reporting boundaries.
- Moved drive data, workflow definitions, contextual help, events, jobs,
  progress, ETA, protection, and scheduling out of UI ownership.
- Added stable physical `DriveId` values independent of current `/dev` paths.
- Added mock providers, capability discovery, structured results, `--dry-run`,
  CMake, and automated tests.
- Retired the legacy frontend-owned live hardware path.
- Kept production hardware mode unavailable until real discovery and safety
  classification are ready.

The 0.2 engineering exit condition was reached: Core logic no longer lives
inside UI code.

## Next: 0.3.0 — Linux Discovery and Safety

- Add real Linux inventory and stable physical-device resolution.
- Detect mounts, system disks, boot and home devices, swap, LVM, RAID, ZFS, and
  VM ownership.
- Classify drives as ready, busy, protected, or unknown before exposing
  actions.
- Revalidate identity and protection state before execution.

## Later Milestones

### 0.4.x — Health Diagnostics

- Add production S.M.A.R.T. capability discovery, health data, error history,
  and self-tests.

### 0.5.x — Performance Diagnostics

- Add read-only throughput, workload, latency, and stall diagnostics for HDDs
  and SSDs.

### 0.6.x — Persistent Jobs

- Add production process supervision, durable job state, controls, and
  reconnectable sessions.

### 0.7.x — Sanitization and Destructive Testing

- Add policy-gated ATA, HPA/DCO, secure erase, overwrite, and write/verify
  workflows with strong confirmation and identity checks.

### 0.8.x to 1.0.0 — Distribution and Stabilization

- Add user configuration, structured reports, dependency checks, provider
  compatibility handling, and conventional packaging.
- Complete safety, interruption, hotplug, terminal, and Linux compatibility
  audits on the path to a stable release.

Source clone, build, test, and run remains the distribution method at this
stage.
