# DriveLab 0.2.0 — Core Architecture Changelog

## 0.2.0

0.2.0 is mostly an under-the-hood update. DriveLab still looks very similar to
0.1.0, but most of the prototype logic has moved out of the TUI and into a
separate Core. This gives the project a cleaner base before real drive
discovery starts in 0.3.0.

### NEW

- Added a CMake/C++20 project with a standalone `drivelab_core` target.
- Added the basic plumbing for capabilities, errors, configuration, logging, process execution and reports.
- Added provider interfaces for inventory, health, ATA/HPA, benchmarks and sanitisation, with mock implementations for the current demo.
- Added stable physical `DriveId` values based on WWN or serial, model, and
  capacity. Current `/dev` paths are kept as observations rather than identity.
- Added a Core-owned `DemoSession` containing the seven approved mock drives,
  feature data, workflow definitions, contextual help, events, jobs, progress,
  ETA, and simulated results.
- Added `--dry-run` for inspecting mock Core capabilities and stable fixture
  identities with process execution disabled.
- Added a native argv-based POSIX process runner for future providers. It is
  isolated from the 0.2 demo and production mode is not connected to it.

### CHANGES

- The TUI now reads drive, feature, workflow, event, job, and progress state
  through Core-backed application interfaces.
- Scan, workflow submission, job controls, progress updates, and termination
  checks now go through Core commands.
- Fresh demo sessions now start with an empty Job Queue. A workflow creates a
  job only after explicit confirmation.
- Demo scheduling now locks by stable `DriveId`: jobs for the same physical
  identity serialize, while jobs for different identities can progress
  together.
- Protected-workflow decisions are enforced in Core rather than relying on the
  frontend to hide controls.
- The TUI remains responsible for ncurses rendering, layout, focus, keyboard
  and mouse input, scrolling, and modal presentation.

### REMOVED

- Removed the legacy frontend-owned live hardware path.
- Removed direct `lsblk`, `smartctl`, `fio`, sysfs, process-control, and storage
  report logic from the frontend runtime.
- Removed any normal-mode fallback to exploratory hardware behavior.

### FIXED / CLEANED UP

- Kept mock storage activity behind application, provider, safety, and job
  boundaries instead of duplicating state in `demo_ui.cpp`.
- Revalidated physical identity before a queued job starts and retained
  same-drive locks while jobs are paused.
- Kept demo and dry-run process/report backends explicitly disabled.
- Preserved the approved 0.1.0 layout and interaction model while replacing its
  internal ownership model.

### TESTING

- Added seven CTest targets covering drive identity, providers, demo state,
  frontend projection, scheduling, safety, modes, interfaces, process handling,
  and the frontend execution boundary.
- Covered same-drive serialization across path changes, cross-drive
  concurrency, pause/resume/cancel, queue handoff, ETA/progress, completion,
  simulated failure, protected-drive rejection, and irreversible-job behavior.
- Added regression checks preventing process APIs, sysfs discovery, storage
  tools, and frontend report logic from returning under `prototype/`.
- Verified CMake configuration, a full Debug build, all 7/7 CTest targets,
  `--version`, `--help`, `--dry-run`, `--demo`, and safe rejection of bare
  production invocation on Linux.

### CURRENT LIMITS

- Production hardware mode is intentionally unavailable in 0.2.0.
- Demo and dry-run use mock data and execute no storage commands.
- Real Linux discovery and production safety classification begin in 0.3.0.
- Real diagnostics, performance tests, persistent jobs, sanitization,
  configuration loading, and report output remain later work.
