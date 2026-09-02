# DriveLab 0.1.0 — Interface Prototype Manual

DriveLab 0.1.0 is the intentional first interface milestone. It is useful for
exploring the layout and workflow model without touching storage hardware.

## Install and Build

On Debian, Ubuntu, or Proxmox:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev

cd 0.1.0/source
g++ -std=c++17 -O0 -g -Wall -Wextra *.cpp -lncursesw -pthread -o drivelab
```

An early bootstrap installer is preserved in `source/install-drivelab.sh`. It
installs a broader set of tools explored during development, but those tools
are not required for the supported 0.1.0 demo workflow.

## Launch

Run the interface prototype as a normal user:

```bash
./drivelab --demo
```

Demo mode does not run `lsblk`, `smartctl`, `fio`, `hdparm`, `nwipe`, `ioping`,
or another storage command. It does not require root or access a real drive.

## Screen Layout

The screen has four main areas:

- a header with the version, session concept, Settings placeholder, and detach
  control;
- a left drive list with up to five visible entries and a scrollbar;
- a selected-drive area with identity, status, feature tabs, and contextual
  content;
- a fixed lower panel for Event Log and Job Queue.

The minimum terminal size is 70 columns by 24 rows. The layout responds to
terminal resizing and shortens secondary labels when space is tight.

## Demo Drives

The canned inventory contains seven examples:

- two ready drives;
- a protected system disk;
- a protected VM-assigned disk;
- a protected ZFS member;
- a failing NVMe example;
- a degraded HDD example.

Protected drives stay visible and explain why they are protected. Their action
workflows are hidden, leaving status and mock health information only.

## Feature Views

### Overview

Shows sample identity, transport, temperature, power-on hours, health counters,
ATA security, and HPA state.

### S.M.A.R.T.

Shows a mock health summary and simulated choices for short, extended,
conveyance, and raw-data workflows.

### ATA / HPA

Demonstrates how security state, HPA, DCO, secure erase, and enhanced secure
erase would be explained and selected.

### Benchmark

Demonstrates sequential, HDD zone, random 4K, and latency profile selection
with representative fake results.

### Sanitize

Compares simulated ATA and `nwipe` methods, including destructive-operation
labels and contextual explanation.

Selecting an option changes focus and help text. Enter or `Open Workflow` opens
a separate confirmation. Confirming it creates a simulation only.

## Event Log and Job Queue

The Event Log shows timestamped interface and simulation events. The Job Queue
shows up to five simulated jobs with state, progress, ETA, and available
controls.

The mock scheduler keeps one running or paused job for a demo device path.
Another job for the same path waits, while jobs for different demo paths can
advance together. This state exists only inside the running process.

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
| `Shift+Q` | Open the terminate confirmation |

The Settings control opens a placeholder dialog in this version.

## Mouse Controls

Mouse clicks can select drives, tabs, options, buttons, and simulated job
controls. Feature-option clicks select without immediately running a workflow.
A control activates only when press and release resolve to the same region.

Hover updates highlighting and contextual text where the terminal reports
mouse motion. The wheel scrolls the drive list, feature content, or lower panel
under the pointer.

Temporary mouse diagnostics are written to:

```text
/tmp/drivelab-demo-mouse.log
```

## Prototype Boundaries

The session and tmux labels demonstrate intended interaction. DriveLab 0.1.0
does not create a persistent service or managed tmux session. Exiting the
standalone program discards simulated jobs and UI state.

The historical source also contains an experimental no-argument Linux path
that directly invokes storage utilities. It lacks the production architecture,
hardware ownership checks, and safety enforcement required for a supported
storage tool. It is preserved for source history and is not the documented
0.1.0 workflow.

Production hardware discovery, diagnostics, benchmarks, sanitisation, and
safety policy begin in later milestones after the Core architecture is in
place.

## Common Problems

- **The interface is clipped:** enlarge the terminal to at least 70x24.
- **Mouse hover is missing:** some terminals do not report mouse motion. Clicks
  and keyboard navigation remain available.
- **Colors look different:** ncurses color support depends on the terminal and
  its current color configuration.
- **The build cannot find ncurses:** install the ncurses development package for
  the Linux distribution.
