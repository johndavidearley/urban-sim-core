# Regional tick profiling

Build explicitly (the target is excluded from normal builds):

```sh
cmake --build build-strict --target UrbanSimCore-regional-benchmark --parallel 4
./build-strict/bin/UrbanSimCore-regional-benchmark checkpoint.json 1000 profile-warm.json warm
./build-strict/bin/UrbanSimCore-regional-benchmark checkpoint.json 1000 profile-cold.json cold
```

Inputs must be valid playable checkpoints with autonomous growth disabled. The
input save is read only. The output includes per-tick rows and mean/median/p95/max
milliseconds. There is no machine-dependent pass/fail timing threshold.

`warm` initializes from one normal session advance, then retains shared playable
state/caches across subsequent ticks. `cold` uses the normal headless session
advance for every tick, including its reconstruction of host-owned caches. Both
use the saved simulation seed, disaster policy and districts. Tick timing excludes
file loading, saving, graphics and inspection. Neither mode claims GUI frame time.

A separate read-only tourism access-plan probe reports its own timing and counts.
It is already part of normal simulation work, so its probe duration must not be
added to tick duration. Service/transit distance-field entries, freight cached
paths, freight routes, gateway offers and hotels describe retained routing/cache
work. They are not cache-hit or rebuild counters; the model exposes no such
counters. Probe time and JSON measurement work are outside the tick timer.

Every measured tick checks visitor demand/cohort conservation, combined resident
and visitor transit capacity, municipal visitor fee/service categories, treasury
category sums and cash flow, cargo delivery conservation and the freight-cache
bound. Nonzero exit status reports a failed invariant. Final world checksum,
funds, tourism and cargo reports allow warm/cold comparisons without timing data.
These checks complement the rendered script's full save/continuation comparisons.

Retain build type, compiler, machine, fixture population/building/map sizes and
raw JSON with any result. Run comparison profiles without another benchmark or
rendered replay competing for CPU. Do not infer a universal frame-rate target or
change costs/capacities from elapsed time alone.

The retained runner `playtest/examples/phase9_profile.py` accepts `--benchmark`,
`--startup-save`, `--legacy-save`, `--mature-save`, `--artifacts` and optional
`--ticks` (default 1000). It runs the six profiles sequentially and requires warm
and cold modes to agree on final world checksum, funds, tourism and cargo data.
It retains the platform/compiler/build configuration and raw profile files.
