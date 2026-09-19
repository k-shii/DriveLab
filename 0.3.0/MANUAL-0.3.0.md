# DriveLab 0.3.0 — Discovery and Safety Manual

DriveLab 0.3.0 discovers real Linux storage and explains its current identity,
ownership and safety status. Production mode is read-only. It does not run
health diagnostics, benchmarks or destructive operations.

## Requirements

The supported platform focus is native Linux with a UTF-8 terminal. The final
source was built and tested on Ubuntu with GNU C++ 15.2.0 and CMake 4.2.3.

Build dependencies:

- CMake and CTest;
- a compiler with C++20 support;
- POSIX threads;
- ncursesw development headers and libraries;
- libudev development headers and libraries;
- pkg-config;
- libblkid development files, version 2.38 or newer.

On Debian/Ubuntu-family systems, the usual package names are `cmake`,
`build-essential`, `libncurses-dev`, `libudev-dev`, `pkg-config` and
`libblkid-dev`. Use the distribution's packages for these dependencies.

OpenZFS development headers and libraries are optional. CMake checks whether
the public libzfs interface can compile and link. If it is unavailable, DriveLab
still builds, but live ZFS evidence is unavailable. That absence cannot be
treated as proof that a disk is unused.

Use at least **70 columns by 24 rows**; a larger terminal makes evidence easier
to read. Windows terminal and SSH clients can differ in rendering and geometry.
A native Ubuntu terminal is the verified visual reference for this release.

## Verified Environments

0.3.0 was built and tested across the following Linux environments. The roles
below describe the qualification coverage, rather than an official support list.

| Environment | Configuration | Used for |
| --- | --- | --- |
| Zorin OS 18 Core — VMware Workstation VM | x86-64; 4 vCPU; 4 GB RAM; 50 GB virtual disk; NVMe-backed host storage | General Linux build/test and portability checks |
| Linux Mint Cinnamon — Proxmox VM | x86-64; 6 vCPU; 12 GB RAM; NVMe-backed virtual storage | Debian/Ubuntu-family VM qualification and application testing |
| Linux Mint 22.3 “Zena” Cinnamon — VMware Workstation VM | x86-64; NVMe-backed host storage | Additional Linux portability, build and application checks |
| Proxmox VE — bare metal | Intel Core i7-7820X class system; 64 GB DDR4; NVMe system/application storage | Host-level ownership, ZFS, VM/QEMU and difficult safety-classification testing |
| Ubuntu 26.04.1 LTS — Dell OptiPlex 3050 bare metal | Intel Core i3-6100; 16 GB DDR4; NVMe boot/application drive; SATA HDD and optical device | Modest-hardware native reference for the production TUI, device-kind handling, resource usage and final 0.3.0 qualification |

The development and virtualization host used Windows 11 x64 (OS build
26100.9457), an Intel Core i5-10600KF, 48 GB DDR4 RAM, VMware Workstation Pro
26H1 and NVMe-backed storage. It hosted Linux VMs for portability/build
verification; **DriveLab itself runs on Linux, not Windows**.

## Build and Test

From the repository root, with the dependencies already available:

```bash
cd 0.3.0/source
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

These commands were verified from a fresh copy of the 0.3.0 public source.
The complete native Linux suite passes **22/22 tests**. The default Linux
configuration includes the production inventory, ncurses interface and tests.

Run the executable directly from the build directory. A system-wide
installation is not needed for the commands below.

## Launch Modes

### Production

```bash
./build/src/drivelab
```

Starts live discovery and opens the production TUI. Physical inventory appears
first. Disk rows show SCANNING while the remaining ownership and safety checks
run; the Event Log records completion or a scan error.

Production reads Linux device and host-usage information, including read-only
signature checks where applicable. It does not modify drives or offer storage
operations. Some observations require privileges an ordinary user may not have.
Permission gaps are reported, and can prevent a READY result; more access does
not override uncertainty or protection.

### Demo

```bash
./build/src/drivelab --demo
```

Opens the simulated interface with seven canned drives. It does not access real
drives or execute storage commands. Diagnostic views, operation choices and
jobs in this mode are simulated.

A fresh demo starts with an empty Job Queue. Confirming a workflow may create
a simulated job. The session remains in memory; it does not survive process
exit despite the demo's detach/session labels.

### Dry Run

```bash
./build/src/drivelab --dry-run
```

Prints the mock inventory and capability list with external process execution
disabled. It does not inspect the host or perform a live safety assessment.

### Help and Version

```bash
./build/src/drivelab --help
./build/src/drivelab --version
```

The version output is `DriveLab 0.3.0`.

## Production Screen

The left side lists physical devices. The selected device's identity and status
appear above its Overview and Evidence tabs. The lower panel contains the Event
Log and Job Queue.

Use Up/Down to move through drives, then Enter to select one. A path such as
`/dev/sda` is a current observation, not a permanent physical identity.

### Overview

Shows the selected device's current path, available model/serial/identifiers,
capacity, kind, transport, status and grouped reasons. Provider coverage explains
which information sources are complete or unavailable.

Missing information is shown as unavailable rather than filled with guesses.
Health remains unassessed in production.

### Evidence

Press **D** or select **Evidence** to inspect the observations and gaps behind
the assessment. Large hosts can produce many records. Use the content scrollbar
or Page Up/Page Down to move through them.

### Status Meanings

| Status | Meaning |
| --- | --- |
| **SCANNING** | Ownership and safety checks are still running. Readiness is unverified. |
| **READY** | Sufficient current evidence that the disk is free of ownership or use. |
| **BUSY** | Positive evidence of current ownership or use. |
| **CAUTION** | DriveLab could not fully establish that the disk is free of use. |
| **RESTRICTED** | Host-critical or system storage, including root/boot backing. |

READY does not mean healthy, and it does not enable an operation in 0.3.0.
CAUTION does not prove that a drive is busy: it can reflect missing access,
ambiguous identity, changing host state or ownership that could not be resolved.
Overview and Evidence explain the available reasons.

### Device Kinds

Device kind is separate from transport. For example, a SATA disk is DISK with a
SATA/ATA transport, while a SATA DVD drive is OPTICAL. USB describes a connection,
not whether the attached device is a disk.

DISK devices enter the normal safety assessment. OPTICAL, FLOPPY and
UNKNOWN_PHYSICAL devices remain visible without a disk readiness verdict.
Optical capacity is shown as reported media size. Missing or conflicting kind
information does not turn a device into a disk candidate.

## Navigation

Production controls:

| Key | Action |
| --- | --- |
| `Tab` / `Shift+Tab` | Move through controls; change tabs when focused on tabs |
| `Up` / `Down` | Move through drives, scroll focused content or the Event Log |
| `Left` / `Right` | Change tabs or confirmation choices |
| `Enter` | Select a drive, enter a view or activate the focused control |
| `Esc` | Move outward or cancel a dialog |
| `O` | Open Overview for the selected device |
| `D` | Open Evidence for the selected device |
| `Page Up` / `Page Down` | Scroll the device content |
| `R` | Request a fresh scan |
| `E` | Show Event Log |
| `J` | Show Job Queue |
| `Q` | Open exit confirmation |

Mouse clicks select drives and activate tabs or buttons. The mouse wheel scrolls
the viewport under the pointer. Keyboard navigation remains available when a
terminal does not provide the expected mouse events.

Settings is a placeholder. Production has no jobs to submit, so its Job Queue
stays empty.

The demo keeps its simulated feature tabs: O for Overview, S for S.M.A.R.T.,
A for ATA/HPA, B for Benchmark and N for Sanitize. Its diagnostic data and
workflows remain canned examples.

## Rescanning and Revalidation

Press **R** or activate **Scan** after a device, mount or ownership change.
DriveLab acquires a fresh snapshot and clears previous readiness while it works.
It does not continuously monitor hotplug.

Selection carries forward only when the same stable identity and supporting
observations still match. Missing, replaced or unresolved devices clear the
selection; a new device at an old path does not inherit the old selection.
Another scan cannot overlap a scan already in progress.

The deeper checks may take time on slower systems or hosts with many processes.
Let the completion event arrive before reading a disk status as a completed
assessment.

## Safe Exit

In production, press **Q**, select **Exit** and confirm with Enter. Cancel is
selected initially; Tab or an arrow key changes the choice. Esc cancels the
dialog. Quitting during acquisition requests cancellation of the pending scan;
an in-progress system call may need to return before shutdown completes.

In demo, Q opens the **Detach** confirmation. Confirming it exits the standalone
demo. Its in-memory session and simulated jobs are discarded.

## Current Limits and Common Questions

- **A disk stays at CAUTION:** inspect Overview and Evidence. Missing permissions,
  incomplete identity or unresolved ownership are meaningful results, not a
  reason to force READY.
- **The system disk is RESTRICTED:** this is expected for host-critical storage.
- **A DVD drive shows OPTICAL:** it is visible for inspection and is outside the
  disk safety workflow.
- **The display is clipped or misaligned:** check terminal size and UTF-8/font
  settings. Some Windows SSH clients render differently from a native Ubuntu
  terminal; terminal portability is planned follow-up work.
- **Health or benchmark screens are missing:** production 0.3.0 has no SMART,
  health tests, benchmarks, sanitisation or destructive testing. Those simulated
  demo screens are not live features.

See the [release overview](README.md), [changelog](CHANGELOG-0.3.0.md) and
[roadmap snapshot](ROADMAP-0.3.0.md) for this version's scope and what follows.
