"""Compare retained journey v1 with a native M54 journey; fail unmet targets."""
import argparse
import csv
import json
from pathlib import Path


def report(run, baseline):
    samples = json.loads((run / "measurements.json").read_text())
    with (run / "money.csv").open() as stream:
        rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
    old = {}
    for sample in json.loads(baseline.read_text()):
        for row in sample["budget"]["history"]:
            old[row["tick"]] = row
    fields = ("tick", "population", "funds", "debt", "revenue", "expenses")
    with (run / "baseline-money.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(old[t] for t in sorted(old))
    by_tick = {r["tick"]: r for r in rows}
    early = [r for r in rows if r["tick"] <= 2400]
    errors = {k: max(abs(r[k] - old[r["tick"]][k]) / max(1, old[r["tick"]][k])
                     for r in early) for k in ("revenue", "expenses")}
    goal = next((s for s in samples if s["population"] >= 10000 and s["uniques"] >= 2), None)
    # Completed ticks ending at the first 10k checkpoint; aggregate smooths tax rounding.
    window = [r for r in rows if goal and goal["tick"] - 100 < r["tick"] <= goal["tick"]]
    ratio = sum(r["revenue"] for r in window) / max(1, sum(r["expenses"] for r in window))
    point_ratio = goal["revenue"] / max(1, goal["expenses"]) if goal else 0
    checks = {
        "continuous_history": [r["tick"] for r in rows] == list(range(1, rows[-1]["tick"] + 1)),
        "no_debt": all(r["debt"] == 0 and r["funds"] >= 0 for r in rows),
        "10000_residents_and_two_uniques": goal is not None,
        "revenue_expense_ratio": 1.2 <= ratio <= 1.6 and 1.2 <= point_ratio <= 1.6,
        "tick_12000_funds": 12000 in by_tick and by_tick[12000]["funds"] <= 3480000,
        "starting_budget": len(early) == 2400 and max(errors.values()) <= .05,
    }
    result = dict(accepted=all(checks.values()), checks=checks, first_goal=goal,
                  revenue_expense_ratio_100_ticks=ratio,
                  revenue_expense_ratio_at_goal=point_ratio,
                  ratio_window_population_range=([min(r["population"] for r in window),
                                                   max(r["population"] for r in window)] if window else []),
                  early_budget_max_relative_error=errors,
                  early_population_and_funds_exact=all(r[k] == old[r["tick"]][k]
                      for r in early for k in ("population", "funds")),
                  funds_at_12000=by_tick.get(12000, {}).get("funds"),
                  baseline_funds_at_12000=old[12000]["funds"])
    (run / "balance-report.json").write_text(json.dumps(result, indent=2) + "\n")
    lines = ["# M54 journey comparison", "", f"Accepted: {result['accepted']}", "",
             f"Revenue / expenses over 100 ticks ending at first 10k checkpoint: {ratio:.4f}.", "",
             "| Tick | V1 population | V2 population | V1 funds | V2 funds |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for tick in (100, 400, 2400, 4000, 8000, 12000, 15800, 15900):
        if tick in old and tick in by_tick:
            a, b = old[tick], by_tick[tick]
            lines.append(f"| {tick} | {a['population']} | {b['population']} | {a['funds']:,} | {b['funds']:,} |")
    lines += ["", "Checks:", ""] + [f"- {key}: {value}" for key, value in checks.items()]
    (run / "balance-report.md").write_text("\n".join(lines) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--baseline", type=Path, required=True)
    args = parser.parse_args()
    result = report(args.run, args.baseline)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["accepted"] else 1)
