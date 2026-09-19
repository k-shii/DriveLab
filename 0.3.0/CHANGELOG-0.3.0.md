# DriveLab 0.3.0 — Discovery and Safety Changelog

## 0.3.0

DriveLab now reads the host's real storage state. Normal launch opens the live
interface, with device identity, ownership checks and an explanation of each
disk's status. Production remains read-only.

### NEW

- Added native Linux physical-device discovery and stable identity resolution.
- Added direct host-usage checks and ownership analysis across storage
  relationships, processes and supported virtual-machine configurations.
- Added the production ncurses interface with real device data.
- Added READY, BUSY, CAUTION and RESTRICTED presentation, with grouped reasons
  in Overview and detailed observations in Evidence.
- Added rescanning and identity revalidation so changed or reused device paths
  do not carry an old selection forward.
- Added device-kind handling for DISK, OPTICAL, FLOPPY and UNKNOWN_PHYSICAL.
  Optical drives stay visible without being treated as HDD/SSD candidates.

### CHANGES / IMPROVEMENTS

- Connected Linux discovery, identity and safety checks to the normal application.
- Split startup into fast inventory followed by deeper ownership and safety
  checks. Devices appear early, with disks marked SCANNING until proof finishes.
- Substantially reduced production scan time. On one Ubuntu test system,
  comparable Debug scans fell from about 50 seconds to about 16 seconds.
  Results depend on the host and workload.
- Reduced repeated evidence lookup and parsing while retaining completeness
  checks, retries and conservative safety decisions.
- Made incomplete evidence and unavailable providers visible without turning
  uncertainty into READY.
- Kept demo and dry-run available with canned data and no real storage commands.

### TESTING / VALIDATION

- Passed the full native Linux suite: **22/22 tests**.
- Completed real bare-metal Ubuntu qualification, including the production TUI.
- Exercised ownership and conservative handling on a live Proxmox host. Opaque
  or incomplete ownership remained non-READY rather than being forced clear.
- Checked production and demo startup, rescan/selection behaviour, CLI modes,
  device-kind handling and exit during acquisition.

### CURRENT LIMITS

Production is inspection-only. SMART/health, benchmarks, persistent production
jobs, sanitisation and destructive testing are still later work. READY describes
ownership/use, not health. Terminal/SSH rendering and scan efficiency will get
further refinement.
