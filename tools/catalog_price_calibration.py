#!/usr/bin/env python3
"""Report fixed M49 prices against retained playable journey measurements."""
import argparse
import json
import re
import statistics
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measurements", type=Path)
    args = parser.parse_args()
    measurements = json.loads(args.measurements.read_text())
    samples = {sample["tick"]: sample for row in measurements for sample in row["budget"]["history"]}
    references = {}
    for population in (200, 500, 1200, 2500, 5000, 10000):
        nearest = sorted(samples.values(), key=lambda sample: (abs(sample["population"] - population), sample["tick"]))[:100]
        references[population] = {
            "surplus": statistics.median(sample["revenue"] - sample["expenses"] for sample in nearest),
            "population_min": min(sample["population"] for sample in nearest),
            "population_max": max(sample["population"] for sample in nearest),
            "samples": len(nearest),
        }
    header = Path(__file__).resolve().parents[1] / "src/gameplay/CatalogPrices.hpp"
    costs = {key: int(value) for key, value in re.findall(r"int64_t (\w+) = (\d+);", header.read_text())}
    tiers = {
        500: ("arterialSegmentCost", "arterialUpgradeCost", "parkTileCost", "cemeteryCost", "recyclingCost"),
        1200: ("officeTileCost", "gasCost", "solarCost"),
        2500: ("substationCost", "hydroCost", "treatmentCost", "crematoriumCost", "incineratorCost"),
        5000: ("hotelCost", "cargoPortCost", "passengerTerminalCost"),
        10000: ("nuclearCost", "airportCost"),
    }
    report = {"method": "100 unique history samples nearest each population, median revenue minus expenses; fixed Phase 13 baseline, not a Phase 14 economy playthrough",
              "source": str(args.measurements), "references": references, "prices": []}
    for population, keys in tiers.items():
        for key in keys:
            price = costs[key]
            surplus = references[population]["surplus"]
            report["prices"].append({"item": key, "tier": population, "price": price,
                                     "reference_surplus": surplus, "surplus_ticks": round(price / surplus, 2)})
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
