# Phase 10: end-to-end playability

Started October 16, 2026, after Phase 9's hosted closeout on `bf8c036`.
M36 is active. This phase validates the player experience built from the existing
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

## Current local checks

The M36 work fixes civic placement on empty zoning (the purchased site is cleared
before private growth), recognizes out-of-order utilities and paused N steps in
the city guide, makes the final guide task a budget review, and shows civic
operating cost in the service tooltip. Native N / period stepping now reports the
completed tick and explicitly says automatic running remains paused.

Local strict checks passed 571 C++ tests and seven Python unit tests. The bounded
rendered regression covers early utilities, N stepping while paused, F6 guide
completion, reopening the guide and the minimum window screenshot. The macOS
Intel local candidate passed 16 installed graphics probes with Vulkan environment
overrides removed, another 16 through its launcher, and native F5/F9 save/load.

The complete rendered rerun passed all 32 tests. The native journey reached
10,093 residents at tick 16,000, with $26,201,436, no debt and full utilities.
A 100-tick loaded continuation matched all measured state; repeated fresh-city
replay checks and hosted CI remain pending. Saved entities are compared by ID
rather than serialization order. HUD projections can recompute on load without
changing the persisted checkpoint or subsequent tick results.
An earlier run was interrupted by a full local disk and is not acceptance evidence.
Generated PPM captures were compressed, retaining PNG previews and logs.

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
