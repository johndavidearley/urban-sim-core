"""Bounded M36 fresh-city journey: native input only, normal funds, no imports.

Construction/policy/time/save/load use keyboard and pointer events. Read-only
inspection and coordinate queries locate controls. The retained scenario can be
replayed with urban-sim-playtest replay. This long acceptance is opt-in.
"""

import argparse
import asyncio
import json
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record


async def main(args):
    s = Session(args.executable, args.artifacts, timeout=120)
    samples = []
    await s.launch()
    await record(s, "start")

    def checkpoint():
        session = json.loads(s.artifact("urban_sim_session.json").read_text())
        city = json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        # Entity containers may serialize in a different order after loading.
        for key in ("buildings", "populationGroups"):
            city[key].sort(key=lambda item: item["id"])
        return session, city

    async def keys(*names):
        await s.request("input", {"events": [{"type": "key", "key": n} for n in names]})

    async def click(x, y):
        await s.request(
            "input",
            {
                "events": [
                    {"type": "move", "x": x, "y": y},
                    {"type": "down", "button": "left", "x": x, "y": y},
                    {"type": "up", "button": "left", "x": x, "y": y},
                ]
            },
        )

    async def control(name):
        c = next(
            c
            for c in (await s.request("inspect", {"section": "ui"}))["controls"]
            if c["name"] == name
        )
        await click(c["x"] + c["width"] // 2, c["y"] + c["height"] // 2)

    async def at(x, y):
        p = await s.request(
            "coordinates", {"direction": "tile_to_screen", "x": x, "y": y}
        )
        assert 0 <= p["x"] < 1280 and 0 <= p["y"] < 740, p
        return {"x": p["x"], "y": p["y"]}

    async def drag(a, b):
        p = await at(*a)
        q = await at(*b)
        await s.request(
            "input",
            {
                "events": [
                    {"type": "move", **p},
                    {"type": "down", "button": "left", **p},
                    {"type": "move", **q},
                    {"type": "up", "button": "left", **q},
                ]
            },
        )

    async def service(kind, x, y):
        if not s.last_state["tools"]["service"]:
            await keys("S")
        for _ in range(12):
            if s.last_state["tools"]["service_type"] == kind:
                break
            await keys("S")
        before = s.last_state["facilities"]
        p = await at(x, y)
        await click(p["x"], p["y"])
        assert s.last_state["facilities"] == before + 1, (
            kind,
            x,
            y,
            s.last_state["toast"],
        )

    async def zone(kind, a, b):
        if not s.last_state["tools"]["zone"]:
            await keys("Z")
        for _ in range(6):
            if s.last_state["tools"]["zone_type"] == kind:
                break
            await keys("Z")
        funds = s.last_state["funds"]
        await drag(a, b)
        assert s.last_state["funds"] < funds, (kind, a, b, s.last_state["toast"])

    async def step(n):
        for _ in range(n // 100):
            await keys(*(["N"] * 100))
        v = s.last_state
        print(
            v["tick"],
            v["population"],
            v["funds"],
            v["debt"],
            v["revenue"],
            v["expenses"],
            v["buildings"],
            v["metrics"]["power_supply_ratio"],
            v["metrics"]["water_supply_ratio"],
            flush=True,
        )
        assert not v["autonomous_growth"]
        assert v["debt"] == 0 and v["funds"] >= 0
        samples.append(v)
        s.artifact("measurements.json").write_text(json.dumps(samples, indent=2))

    try:
        await control("size_96")
        await control("new")
        if not s.last_state["paused"]:
            await keys("Space")
        await keys("F3", "F4", *(["Minus"] * 14), *(["Down"] * 20), "R")
        await drag((32, 48), (64, 48))
        await service(4, 47, 47)
        await service(5, 49, 47)
        for y in (47, 49):
            await zone(1, (32, y), (46, y))
            await zone(2, (50, y), (56, y))
            await zone(3, (58, y), (64, y))
        await keys("F6")
        for i, target in enumerate((10, 12, 12, 12, 12)):
            while s.last_state["budget"]["tax_percent"][i] < target:
                await control("tax_" + str(i) + "_plus")
        await keys("Escape")
        await step(100)
        await service(3, 46, 49)
        await service(2, 48, 49)
        for _ in range(3):
            await step(100)
        for _ in range(20):
            await step(100)
        await service(0, 47, 49)
        await service(1, 57, 47)
        await keys("R")
        if s.last_state["tools"]["road_class"] != "arterial":
            await keys("A")
        await drag((32, 48), (64, 48))
        await drag((32, 44), (64, 44))
        await drag((48, 44), (48, 48))
        for y in (43, 45):
            await zone(1, (32, y), (46, y))
            await zone(5, (50, y), (56, y))
            await zone(3, (58, y), (64, y))
        for _ in range(30):
            await step(100)
        for kind, x, y in (
            (6, 47, 43),
            (7, 49, 43),
            (8, 47, 45),
            (9, 49, 45),
            (10, 57, 43),
            (1, 48, 43),
            (0, 57, 45),
            (7, 49, 49),
        ):
            await service(kind, x, y)
        await zone(4, (32, 46), (44, 46))
        await keys("R")
        if s.last_state["tools"]["road_class"] != "arterial":
            await keys("A")
        await drag((0, 48), (32, 48))
        for y in (52, 56, 60):
            await drag((32, y), (64, y))
        await drag((57, 48), (57, 60))
        for kind, x, y in (
            (4, 47, 51),
            (5, 49, 51),
            (2, 47, 55),
            (3, 49, 55),
            (7, 47, 59),
            (8, 49, 59),
            (1, 31, 52),
            (0, 31, 56),
            (6, 31, 60),
        ):
            await service(kind, x, y)
        for y in (51, 53, 55, 57, 59, 61):
            await zone(1, (32, y), (46, y))
            await zone(5, (50, y), (56, y))
            await zone(3, (58, y), (64, y))
        for _ in range(80):
            await step(100)
        await keys("F6")
        for i, target in enumerate((5, 8, 8, 8, 5)):
            while s.last_state["budget"]["tax_percent"][i] > target:
                await control("tax_" + str(i) + "_minus")
        await keys("Escape")
        await keys("R")
        if s.last_state["tools"]["road_class"] != "arterial":
            await keys("A")
        await drag((8, 44), (32, 44))
        for y in (52, 56, 60):
            await drag((8, y), (30, y))
        await drag((16, 44), (16, 60))
        for kind, x, y in (
            (4, 20, 47),
            (5, 22, 47),
            (2, 20, 43),
            (3, 22, 43),
            (0, 24, 43),
            (1, 26, 43),
            (6, 20, 59),
            (7, 22, 59),
            (8, 24, 59),
            (9, 26, 59),
            (10, 28, 59),
        ):
            await service(kind, x, y)
        for y in (43, 45, 47, 49, 51, 53, 55, 57, 59, 61):
            await zone(1, (8, y), (15, y))
            await zone(1, (17, y), (19, y))
            start = 28 if y == 43 else 23 if y == 47 else 29 if y == 59 else 21
            await zone(1, (start, y), (30, y))
        # Retail demand remains high while the first neighborhood matures.
        # Add a separate serviced shopping street rather than rezone homes.
        await keys("R")
        if s.last_state["tools"]["road_class"] != "arterial":
            await keys("A")
        await drag((64, 48), (66, 48))
        await drag((66, 40), (66, 48))
        await drag((32, 40), (66, 40))
        for kind, x, y in (
            (4, 47, 39),
            (5, 49, 39),
            (2, 47, 41),
            (3, 49, 41),
            (0, 48, 41),
            (1, 48, 39),
        ):
            await service(kind, x, y)
        for y in (39, 41):
            await zone(2, (32, y), (46, y))
            await zone(2, (50, y), (64, y))
        for _ in range(150):
            await step(100)
            if s.last_state["population"] >= 10000:
                break
        assert (
            s.last_state["population"] >= 10000
        ), "Native journey did not reach 10,000 within its tick bound"
        await keys("F4", "F5")
        await s.screenshot("m36-ten-thousand")
        saved_checkpoint = checkpoint()
        baseline = {
            k: s.last_state[k]
            for k in (
                "tick",
                "population",
                "funds",
                "debt",
                "buildings",
                "facilities",
                "metrics",
                "budget",
            )
        }
        await step(100)
        future = {k: s.last_state[k] for k in baseline}
        assert s.last_state["population"] >= 10000
        await keys("F9")
        # Loading recalculates HUD projections (including next-tick utility
        # demand after mortality). The persisted checkpoint must stay exact.
        await keys("F5")
        assert (
            checkpoint() == saved_checkpoint
        ), "Native F9 changed the persisted checkpoint"
        await step(100)
        assert {
            k: s.last_state[k] for k in baseline
        } == future, "Save continuation diverged"
        assert s.last_state["metrics"]["power_supply_ratio"] == 1
        assert s.last_state["metrics"]["water_supply_ratio"] == 1
        await keys("F5")
        await s.screenshot("m36-continuation")
    finally:
        try:
            if not s.broken:
                if s.last_state.get("phase") != "start_screen":
                    await keys("F5")
                result = await record(s, "stop", "m36-native-journey")
                print(json.dumps(result), flush=True)
            print("RUN", s.run_dir, flush=True)
        finally:
            await s.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--executable", default="build-strict/bin/UrbanSimCore-playtest"
    )
    parser.add_argument("--artifacts", default="playtest-artifacts/m36-native")
    asyncio.run(main(parser.parse_args()))
