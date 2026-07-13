# Project Status

Last verified: July 13, 2026

This is the authoritative source for the repository's current implementation
and validation status. `ROADMAP.md` describes milestone history and future
ideas; `IMPLEMENTATION_STATUS.md` and `NEXT_STEPS.md` are retained as historical
development logs and should not be used to determine current priorities.

## Current State

The headless simulation engine and optional visualization stack are functional.
Implemented systems include world generation, zoning and growth, population,
traffic and commute routing, economy and trade, services and utilities
(including power generation capacity/source mix, water, and sanitation coverage),
district policy, public transit, disasters, crime and health, persistence,
replay verification, micro-traffic, metrics, CLI reporting, PPM rendering, and
the optional SDL2 visualizer.

Phase 1 through Phase 5 milestone work recorded in the roadmap is complete.
New work is post-backlog hardening, maintainability, profiling, and model
iteration rather than completion of a missing MVP subsystem.

## Validation Baseline

- 319 tests across the GoogleTest suites.
- Tests are discovered individually by CTest.
- Regular and warnings-as-errors builds pass.
- Full ASan/UBSan and ThreadSanitizer runs pass.
- GitHub Actions runs strict Linux, macOS, and Windows jobs plus Linux
  ASan/UBSan and TSan jobs.
- CMake presets provide matching `regular`, `strict`, `asan`, and `tsan`
  configure/build/test workflows.

The authoritative live test list is produced by:

```bash
ctest --test-dir build --show-only
```

## Current Priorities

1. Split oversized orchestration and CLI translation units, especially
   `CitySimulator.cpp`, `main.cpp`, and the larger reporting modules.
2. Benchmark service-cache fingerprint overhead at large city scale and retain
   correctness while minimizing validation cost.
3. Add MSVC coverage when Windows support becomes a release requirement; Linux
   GCC and macOS AppleClang are covered today.
4. Continue model calibration and visualization work based on concrete product
   goals rather than the obsolete backlog ordering.

## Document Roles

- `STATUS.md`: current implementation, validation baseline, and priorities.
- `ROADMAP.md`: milestone definitions, completed work, and future ideas.
- `ARCHITECTURE.md`: intended system boundaries and data flow.
- `MVP_SPEC.md`: original product scope and success criteria.
- `IMPLEMENTATION_STATUS.md`: historical implementation journal.
- `NEXT_STEPS.md`: historical backlog sequence.
