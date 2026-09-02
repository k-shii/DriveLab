# Changelog

This file tracks the versions prepared in this repository. Each version
directory keeps its own frozen changelog.

## DriveLab 0.2.0 — Core Architecture

0.2.0 is mostly an under-the-hood update. The interface still looks much like
0.1.0, but the demo's drive model and simulated workflow behavior now live in
a separate Core.

- Added CMake/C++20 Core and frontend targets.
- Added stable physical `DriveId`, provider interfaces, mock providers,
  capability discovery, structured results, and `--dry-run`.
- Moved demo workflows, events, jobs, progress, ETA, protection checks, and
  scheduling out of frontend ownership.
- Changed fresh demo sessions to start with an empty Job Queue and create work
  only after explicit workflow confirmation.
- Retired the frontend-owned live hardware path and made unsupported production
  invocation fail safely.
- Added seven CTest targets covering Core behavior, process isolation, mode
  dispatch, and the frontend execution boundary.

Read the [full 0.2.0 changelog](0.2.0/CHANGELOG-0.2.0.md).

## DriveLab 0.1.0 — Interface Prototype

- Established the DriveLab terminal layout and interaction model.
- Added seven canned drive examples with ready, protected, degraded, and
  failing states.
- Added Overview, S.M.A.R.T., ATA/HPA, Benchmark, and Sanitize views.
- Added contextual descriptions and explicit simulated workflow confirmation.
- Added Event Log and Job Queue views with simulated scheduling and progress.
- Added keyboard navigation, mouse interaction, scrolling, responsive layout,
  and protected-drive presentation.
- Preserved early exploratory Linux integration code as historical source, not
  as production storage functionality.

Read the [full 0.1.0 changelog](0.1.0/CHANGELOG-0.1.0.md).
