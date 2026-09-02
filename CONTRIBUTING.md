# Contributing

DriveLab is still an early solo-maintained project, but bug reports, testing
and focused technical contributions are welcome.

If you run into a problem, please include enough information to reproduce it
where possible — Linux distribution, terminal, storage hardware/transport,
what you were doing, and what happened.

## Code Contributions

DriveLab is changing fairly quickly before 1.0, so for larger changes it is
best to open an issue or discussion first.

A few general rules:

- Keep storage operations out of UI code.
- Avoid changing published historical version snapshots.
- Prefer mock data or disposable hardware for testing.
- Be especially careful with anything that can modify or erase a drive.
- Keep documentation clear about what is real, simulated, or unfinished.

Small fixes and focused improvements are easier to review than large unrelated
changes bundled together.

## Building

For the current published 0.1.0 interface prototype:

```bash
cd 0.1.0/source
g++ -std=c++17 -O0 -g -Wall -Wextra *.cpp -lncursesw -pthread -o drivelab
./drivelab --demo
```

## License
Licensing and contribution terms are still being decided.
For substantial contributions, please discuss the change first.