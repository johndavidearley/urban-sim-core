# Export volume pricing (M51)

Player cities receive the existing $40 economy price ($0.40 treasury dollars)
per exported good through 2,000 goods per tick. Above that volume, the average
unit price is $0.40 multiplied by 2,000 / exported volume. Export income therefore
plateaus at $800 per tick at normal prices. More exports never reduce income.
Inflation multiplies the price and ceiling. Import prices are unchanged.

Both surplus trade and finite cargo logistics use the same calculation. Finite
logistics prices delivered exports, not requested or unfulfilled exports.
Autonomous simulation retains flat export pricing; player sessions and their
headless continuations use volume pricing.

The budget Taxes tab shows export volume and average price beneath EXPORTS.
MCP `budget.preview` and `budget.last_tick` expose `export_volume` and
`export_unit_price`, in treasury dollars per good. The ledger saves the completed
measurement; older saves without it load zero values until a new tick completes.
Current projections are derived again on load. No city snapshot fields change.

## Verification

November 3, 2026: strict MSVC Release build passed; all 653 CTest cases and seven
Python unit tests passed. The rendered M51 test passed, and its 800×640 budget
capture was visually inspected. Evidence:
`build/m51-acceptance/20261103-070721-db305efd/`.

The rendered regression `test_export_volume_prices_budget_and_checkpoint`
creates a controlled industrial fixture, verifies 10,000 goods at $0.080 and
$800 revenue, captures the minimum-size budget panel, and checks completed-tick
measurements after save/load. C++ tests cover threshold boundaries, zero trade,
inflation, unchanged imports, autonomous flat pricing, delivered cargo pricing,
treasury conversion and ledger persistence/validation.

November 5, 2026: M51 journey acceptance passed. Because the original artifacts
were absent, rebuilt the Phase 13 executable from `6ac65ef` and ran the unchanged
native journey with optional checkpoint retention. It reproduced 10,324 residents
at tick 15,900 with no debt. The checkpoints preserve the original purchases;
the historical executable is needed because M49 now locks some of those tools.

The current-rule audit reloads each of 160 checkpoints, then advances its native
tick segment using `GameplaySessionSystem::advance`. Across 16,000 audited ticks,
all 5,001 ticks starting or ending below 2,000 residents retain exactly the same
export income as flat pricing. Exports peak at 1,152 goods, including the tick
that crosses 2,000 residents. All 214 ticks starting and ending at or above
10,000 residents earn $800 in exports, below the $907 limit (half of the original
$1,814). The 2,000-good threshold is accepted.

This checks current-rule continuations of historical cities. It does not claim
that the old construction schedule is playable under M49 gates. M54 still owns
the revised journey and integrated balance.

The strict AppleClang Release build and all 653 CTest cases passed, as did seven
Python tests and the rendered M51 budget/save-load regression. The 800×640 logical
window budget capture was visually inspected. Journey evidence is under
`playtest-artifacts/m51-journey/20261105-161754-21c13cef/`; rendered evidence is
under `playtest-artifacts/m51-rendered/20261105-162025-31208d73/`. Hosted CI,
sanitizers and release packaging were not repeated.

## Reproduction

Build the historical `6ac65ef` source in a separate directory with the graphics
dependencies from [PLAYTESTING.md](PLAYTESTING.md). Run the current Python runner
against that executable:

```sh
PYTHONPATH=playtest .venv/bin/python playtest/examples/phase10_native_journey.py \
  --executable build/m51-baseline/bin/UrbanSimCore-playtest \
  --artifacts playtest-artifacts/m51-journey --retain-checkpoints
cmake --build build-strict --target UrbanSimCore-export-journey-audit
build-strict/bin/UrbanSimCore-export-journey-audit \
  playtest-artifacts/m51-journey/RUN/segments.json \
  playtest-artifacts/m51-journey/RUN/export-ticks.csv
```

Replace `RUN` with the directory printed by the native runner. The audit writes
one CSV row per tick and a JSON summary to stdout. It fails if early exports
cross the threshold or lose revenue, late export revenue exceeds $907, or the
retained cities do not cover the upper early-city range and 10,000 residents.
