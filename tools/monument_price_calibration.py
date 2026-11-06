#!/usr/bin/env python3
"""Check M53 monument prices against the retained 10k journey surplus."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ticks",type=Path)
    args=parser.parse_args()
    with args.ticks.open() as stream:
        rows=list(csv.DictReader(stream))
    nearest=sorted(rows,key=lambda row:(abs(int(row["population"])-10000),int(row["tick"])))[:100]
    surplus=statistics.median(int(row["surplus"]) for row in nearest)
    header=Path(__file__).resolve().parents[1]/"src/gameplay/Monuments.hpp"
    entries=re.findall(r'\{\d+,"([^"]+)","[^"]+",\{\{[^}]+\}\},(\d+),(\d+),',header.read_text())
    assert surplus>0 and len(entries)==4
    prices=[{"key":key,"cost":int(cost),"upkeep":int(upkeep),"surplus_ticks":int(cost)/surplus}
            for key,cost,upkeep in entries]
    assert all(item["surplus_ticks"]>=2000 for item in prices),prices
    print(json.dumps({"source":str(args.ticks),"method":"100 current-rule journey continuation ticks nearest 10,000 residents; historical construction, not integrated Phase 14 balance",
        "population_min":min(int(row["population"]) for row in nearest),
        "population_max":max(int(row["population"]) for row in nearest),"median_surplus":surplus,"prices":prices},indent=2))


if __name__=="__main__":
    main()
