# DriveLab 0.1.0 — Interface Prototype Changelog

## 0.1.0

This release establishes the DriveLab interface and workflow model.

### Interface

- Added a responsive ncurses layout with a 70x24 minimum size.
- Added a five-entry drive viewport with seven canned HDD, SSD, and NVMe
  examples.
- Added ready, protected, degraded, and failing drive presentations.
- Added Overview, S.M.A.R.T., ATA/HPA, Benchmark, and Sanitize views.
- Added contextual descriptions and explicit simulated workflow confirmation.
- Added protected-drive status presentation with action workflows hidden.

### Interaction

- Added keyboard-only navigation through drives, tabs, options, controls, and
  dialogs.
- Added mouse selection, hover, wheel scrolling, and matched press/release
  activation.
- Added Event Log and Job Queue views with five-row scrolling viewports.
- Added simulated job progress, pause, resume, stop, cancellation, same-path
  queueing, and cross-path progress.
- Added temporary mouse diagnostics at
  `/tmp/drivelab-demo-mouse.log`.

### Scope

- All documented workflows use mock data and simulation.
- Production hardware discovery, diagnostics, benchmarks, sanitisation,
  persistent jobs, and safety enforcement are not part of 0.1.0.
- Early exploratory Linux integration code remains in `source/` as historical
  implementation material, not as a supported production feature.
