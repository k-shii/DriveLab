# DriveLab Roadmap at 0.1.0

This file preserves the public project direction at the 0.1.0 milestone.

## Completed: Interface Prototype

- Establish the main terminal layout and navigation model.
- Demonstrate drive identity, status, and protected-drive presentation.
- Demonstrate Overview, S.M.A.R.T., ATA/HPA, Benchmark, and Sanitize views.
- Demonstrate contextual help, Event Log, Job Queue, and simulated workflows.
- Keep hardware access outside the documented milestone.

## Next: Core Architecture

- Separate the TUI from device, job, safety, process, configuration, logging,
  and report logic.
- Define small provider interfaces and mock implementations.
- Add stable physical-drive identity independent of `/dev/sdX`.
- Add dry-run behavior, capability discovery, structured errors, and automated
  tests.

## Later Milestones

- Add production Linux discovery and device ownership protection.
- Add fuller S.M.A.R.T. diagnostics and self-tests.
- Add read-only performance and latency diagnostics.
- Add supervised jobs that can survive UI detach and reconnect.
- Add destructive workflows only after safety policy and identity revalidation
  are ready.
- Add configuration, reports, dependency checks, packaging, and release
  hardening on the path to 1.0.0.

Source build remains the distribution method at this stage.
