# Phase 10: end-to-end playability

Started October 16, 2026, after Phase 9's hosted closeout on `bf8c036`.
M36 and Phase 10 are complete. This phase validates the player experience built from the existing
simulation rather than adding another simulation domain.

## M36: fresh city to 10,000 residents

A repeatable native-control scenario starts at the New City screen with the
normal $50,000 budget and no imported saves, entities or liquidity. Road, zone,
service and policy changes use player keyboard/pointer events. Inspection and
coordinate queries may observe state; deterministic N stepping advances time.
Autonomous building (G-mode) stays off. Automatic transit under normal playable
rules is allowed. Record setup, growth, bottleneck diagnoses, recovery and the
10,000-resident checkpoint, then verify continued operation and save/load.

Acceptance requires a retained fresh-city playthrough, useful feedback for the
blocking decisions it exposes, a bounded repeatable acceptance test, and an
installed/package smoke-tested build with clear launch/save instructions.
Population alone is insufficient: report funds/debt, utilities, jobs/commuting,
services and reproducibility. Fix only demonstrated control, feedback, layout or
progression problems, and do not silently relax the scenario with fixture money
or demographic imports. Native-control coverage and semantic setup must remain
explicitly distinguishable in evidence.

## Delivery

1. Establish the native-control baseline and retain screenshots/decision points.
2. Resolve demonstrated discoverability, feedback and minimum-window issues.
3. Retain replay/save-continuation acceptance for the successful journey.
4. Build an installed release candidate for the current host, verify its launch
   and save behavior, and document platform/architecture and runtime contents.
5. Run relevant local checks and hosted CI on the final implementation commit.

A packaged local candidate is not a signed/notarized public release. No new
renderer, individual citizen simulation or additional transport domain is part
of M36. Future milestones will be defined from observed player needs.

## Verification and retained evidence

Implementation commit: `ae9ccb9e6433da7b15be9cbe1663f1b7d05ccb62`.
Local evidence is retained in `playtest-artifacts/m36-20261016/REPORT.md`.

The native runner passed from a fresh city to 10,093 residents at tick 16,000,
with $26,201,436 and no debt. Another 100 ticks reached 10,379 residents and
$26,467,325, with revenue $5,078 and expenses $2,319. Native F9/F5 preserved the
saved checkpoint (entity arrays normalized by ID), and subsequent simulation
matched the uninterrupted run exactly. Immediate HUD projections can recompute
on load without changing the persisted world or later tick results.

Two independent 852-step replays passed all measured-state assertions. Both and
the accepted runner produced canonical SHA-256
`c1a7bfbdab85f0873c8323eebe415caa58c2e70efda88b3542c64f548ea4f261`.
No imported population, construction, liquidity or G-mode was used. The record
contains native construction, policy, N stepping and F5/F9, with read-only
inspection/coordinate queries and explicit assertions for replay verification.

At the final checkpoint, power/water coverage and supply were 100%, the road
freight gateway was connected, no goods were unrouted and deathcare backlog was
zero. All 7,000 jobs were occupied; housing capacity was 10,752. Ongoing player
pressures include 32.6% unemployment, 0.826 congestion, incomplete civic/waste
coverage and elevated illness. Reaching the milestone does not imply an
issue-free city; the report retains those conditions and the construction/tax
choices that enabled growth.

Demonstrated fixes clear old zoning when a civic facility is purchased, recognize
early utilities and paused N steps in the city guide, make its final task a
budget review, and expose service upkeep in the tool tooltip. Native N / period
stepping reports the completed tick while stating that automatic running is
paused; the title also explains N stepping.

Local strict checks passed 571 C++ tests, seven Python unit tests and all 32
rendered Vulkan/GLFW acceptance tests. The guide regression covers early
utilities, paused stepping, F6 completion, reopening and minimum-window layout.
An earlier disk-interrupted run is excluded; completed diagnostic traces and
PPM captures were compressed while retaining PNG previews and logs.

The clean-source macOS Intel rc2 ZIP is retained under
`playtest-artifacts/m36-20261016/package/`. Its installed build passed 16 graphics
probes without Vulkan environment overrides and native F5/F9 save/load. The
actual ZIP was extracted into a path containing a space; its launcher passed
another 16 graphics probes with an isolated save directory. Its manifest records
the implementation commit, architecture, source cleanliness and file hashes.
Archive SHA-256:
`0c891540c1396f75ba725554a7033e338649f3b1324deb636b7a67346e8338c0`.

Hosted CI passed on the implementation commit in
[run 38981010151](https://github.com/johndavidearley/urban-sim-core/actions/runs/38981010151):
strict Windows/macOS/Linux builds/tests, ASan/UBSan, ThreadSanitizer, X11/Wayland
rendered and graphics checks, installed macOS graphics smoke and install layout.

## Reproduce

From the repository root, with the playtest Python environment installed:

```sh
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase10_native_journey.py \
  --executable build-strict/bin/UrbanSimCore-playtest \
  --artifacts playtest-artifacts/m36-native
```

The runner uses a fixed 96x96 New City, normal initial funds, native construction
and tax controls, and a bounded number of N steps. Every service purchase and
zoning drag must spend funds successfully. It records measurements and a replay
scenario; failures retain their trace rather than relaxing acceptance.

To package an existing Release build on macOS:

```sh
cmake --build build-graphics-release --parallel 4
.venv/bin/python scripts/package_macos.py --output playtest-artifacts/m36-package
```

The ZIP includes a launcher, save-location instructions, bundled graphics runtime,
file hashes and source revision/dirty state. Extract it as a directory. The launcher
uses `~/Library/Application Support/UrbanSimCore`, optionally overridden with
`URBAN_SIM_SAVE_DIR`. F5/F9 save/load a session plus its `.city.json` companion.
The candidate is for the packaging host's architecture; it is ad-hoc signed and
not notarized for public distribution.
