# Municipal bonds (M44)

Unpaid ticks still become overdraft at 0.1% per tick. A bond lets the player
borrow ahead of a project instead, on a term they choose.

The budget panel's BONDS tab issues one bond at a time. The principal is
$10,000, $50,000, or $200,000. The term is 100, 200, or 400 ticks. Issuing adds
the principal to cash at once, outside the tick totals, like a tool purchase.

Each tick pays one equal part of the principal, plus 0.15% interest on the
principal still owed. The parts use cumulative rounding, so they add up to the
principal exactly. Whole dollars of interest are charged; the fraction carries
to the next tick. A $50,000 bond over 200 ticks pays $250 plus $75 on its first
tick.

The installment comes before ordinary expenses. Cash that cannot cover it joins
the existing overdraft, once, and the bond stays on its schedule. Surplus never
prepays the bond. PAY OFF BOND pays the remaining principal from cash and
forgives the interest fraction. The button stays disabled until cash covers it.

The next-tick estimate has a BOND PAYMENT row, and its surplus includes it. The
last-tick ledger shows what was paid, what was missed, and the principal before
and after. It reconciles as cash before − bond paid + revenue − expenses +
borrowed − repaid = cash after, and debt before + bond missed + borrowed +
interest − repaid = debt after.

MCP `budget` adds `bonds`, `issue_bond` with `bond_principal` and `bond_term`,
and `pay_off_bond`. It rejects a second bond and values outside the catalog.
Inspection reports `bond`, `preview.bond`, and `last_tick.bond`.

Sessions save `bond` with `principal`, `term`, `ticksElapsed`, and
`interestRemainder`, and the ledger's bond line. A city with no bond writes
neither, so its saves match earlier versions, and legacy saves load with no
bond. A malformed bond, or a ledger bond line that does not reconcile, rejects
the session before the city changes.

Both playable hosts run the same tick, so the visualizer and
`--simulate-session` produce the same ledger. The headless run prints a BOND
line while a bond is open. `CitySimulator` has no bonds: a default
`--simulate` run is unchanged.

There is no bond market, no credit rating, no second open bond, and no
calendar. The term is a tick count.
