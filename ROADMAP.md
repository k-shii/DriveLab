# DriveLab Roadmap

DriveLab is being built in small versioned stages. The interface came first,
followed by the Core boundaries beneath it. Real Linux discovery and safety
classification are now in place; health diagnostics come next.

## 0.3.0 — Discovery and Safety ![COMPLETED](https://img.shields.io/badge/COMPLETED-brightgreen)

- ☑ Add native Linux physical-device discovery and stable identity resolution.
- ☑ Resolve direct host use and storage ownership, including supported VM configurations.
- ☑ Present READY, BUSY, CAUTION and RESTRICTED with evidence behind each result.
- ☑ Add the read-only production TUI, phased scanning and rescanning.
- ☑ Keep optical and other non-disk device kinds separate from disk assessment.

## 0.4.x — Health Diagnostics (next)

- ☐ Add SMART capability detection, health data and clear health explanations.
- ☐ Add supported self-tests and useful progress/results.
- ☐ Further improve production-scan efficiency and lightweight operation.
- ☐ Refine startup/initialization, terminal/SSH compatibility and device-kind presentation.

## 0.5.x — Performance Diagnostics

- ☐ Add read-only performance and latency diagnostics.

## 0.6.x — Persistent Jobs

- ☐ Add supervised persistent jobs and reconnectable sessions.

## 0.7.x — Sanitization / Destructive Testing

- ☐ Add sanitisation only after target checks and destructive-operation policy are ready.

## 0.8.x to 1.0.0 — Distribution and Stabilisation

- ☐ Add reports, capability checks, configuration, and conventional packaging.
- ☐ Audit safety, interruption handling, terminal behavior, and Linux compatibility.
- ☐ Reach a stable Linux terminal release without making specialist tools part of the minimum install.

For now, distribution remains source clone, pull, build, and run.

Beyond 1.0, further storage capabilities and optional graphical frontends can
build on stable interfaces. Service and remote/web clients are later directions;
the terminal remains a first-class client.

## Completed

### 0.2.0 — Core Architecture

- ☑ Separate Core, TUI, providers, jobs, safety, process execution, configuration, logging, and reporting.
- ☑ Move demo state and simulated workflows behind Core/application interfaces.
- ☑ Use stable physical-drive identity for Core ownership and scheduling.
- ☑ Add capability discovery.
- ☑ Add mock providers.
- ☑ Add dry-run behavior that cannot execute external storage commands.
- ☑ Add automated Core and architecture-boundary tests.
- ☑ Retire the legacy frontend-owned live hardware path.
- ☑ Keep production hardware mode unavailable until discovery and safety are ready.

### 0.1.0 — Interface Prototype

- ☑ Define the terminal layout, navigation, storage views, and workflow shape.
- ☑ Demonstrate protected devices, contextual help, events, jobs, and operation choices with mock data.
- ☑ Keep all published workflows simulated.
