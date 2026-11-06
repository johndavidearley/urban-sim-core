#!/usr/bin/env python3
"""Check fixed M52 prices against 100 journey ticks nearest each achievement."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("journey_report",type=Path)
    parser.add_argument("ticks",type=Path)
    parser.add_argument("visitor_report",type=Path)
    args=parser.parse_args()
    report=json.loads(args.journey_report.read_text())
    rows=list(csv.DictReader(args.ticks.open()))
    header=Path(__file__).resolve().parents[1]/"src/gameplay/UniqueBuildings.hpp"
    costs={key:int(cost) for key,cost in re.findall(
        r'\{\d+,\s*"([^"]+)",\s*"[^"]+",\s*"[^"]+",\s*\d+,\s*(\d+),',header.read_text())}
    results=[]
    for unlock in report["unlocks"]:
        nearest=sorted(rows,key=lambda row:(abs(int(row["tick"])-unlock["tick"]),int(row["tick"])))[:100]
        surplus=statistics.median(int(row["surplus"]) for row in nearest)
        results.append({**unlock,"cost":costs[unlock["key"]],"median_surplus":surplus,
                        "surplus_ticks":costs[unlock["key"]]/surplus})
    visitor=json.loads(args.visitor_report.read_text())
    assert visitor["cost"]==costs["visitor_pavilion"]
    results.append({"key":"visitor_pavilion","tick":visitor["earned_tick"],"cost":visitor["cost"],
                    "median_surplus":visitor["median_surplus"],"surplus_ticks":visitor["surplus_ticks"]})
    assert len(results)==6 and all(300<=item["surplus_ticks"]<=600 for item in results),results
    print(json.dumps({"method":"100 current-rule continuation ticks nearest first unlock; visitor branch buys an accessible park and enables tourism at 5,000 residents; fixed historical construction, not integrated Phase 14 balance", "prices":results},indent=2))


if __name__=="__main__":
    main()
