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

## Verification and remaining acceptance

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

The $800 ceiling is below M51's $907 late-city acceptance limit (half of the
retained journey's $1,814). The 2,000-good threshold is provisional: the retained
Phase 13 journey artifacts referenced in STATUS.md are absent from this checkout.
The journey must still be rerun to verify that exports below 2,000 residents
remain below the threshold. M51 is not closed until that check passes.
