# DriveLab Roadmap at 0.3.0

This file preserves the public project direction at the 0.3.0 milestone.

## Completed

### 0.1.0 — Interface Prototype

- Established the terminal layout, navigation and simulated storage workflows.

### 0.2.0 — Core Architecture

- Moved drive data, jobs, safety and simulated workflows behind a separate Core.
- Added mock/dry-run modes, CMake and automated tests.

### 0.3.0 — Discovery and Safety

- Added real Linux discovery, stable identity, host usage and ownership analysis.
- Added conservative safety classification and the read-only production TUI.
- Added evidence views, phased scanning, rescanning and device-kind handling.

## Next: 0.4.x — Health Diagnostics

- Add SMART capability detection, health data and useful health explanations.
- Add supported self-tests and make their progress/results understandable.
- Further reduce scan cost and keep routine operation lightweight.
- Improve startup and initialization feedback.
- Refine terminal/SSH compatibility and device-kind presentation.

## Later Milestones

### 0.5.x — Performance Diagnostics

- Add read-only throughput, workload, latency and stall diagnostics.

### 0.6.x — Persistent Jobs

- Add supervised jobs, durable results and reconnectable sessions.

### 0.7.x — Sanitization / Destructive Testing

- Add sanitisation and write/verify workflows after target checks, confirmation
  and destructive-operation policy are ready.

### 0.8.x to 1.0.0 — Distribution and Stabilisation

- Add reports, configuration, dependency/capability checks and conventional packaging.
- Audit safety, interruption handling, terminal behaviour and Linux compatibility.
- Reach a stable Linux terminal release while keeping specialist tools optional.

Source clone, build, test and run remains the distribution method at this stage.

### Beyond 1.0

- Expand useful storage capabilities while keeping the TUI a first-class client.
- Establish stable interfaces for optional graphical frontends, with service
  and remote/web clients as later directions.
