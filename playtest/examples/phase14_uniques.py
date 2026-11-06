"""M52: build two earned uniques in the retained journey using native input.

The source is an ordinary Phase 13 journey save, not a catalog/funds fixture.
M54 retains the new construction schedule and integrated balance playthrough.
"""
import argparse
import asyncio
import json
from pathlib import Path
from urban_sim_mcp.session import Session


async def main(args):
    s = Session(args.executable, args.artifacts, timeout=120)
    await s.launch()
    async def keys(*names):
        await s.request("input", {"events": [{"type": "key", "key": key} for key in names]})
    async def checkpoint():
        await keys("F5")
        return (json.loads(s.artifact("urban_sim_session.json").read_text()),
                json.loads(s.artifact("urban_sim_session.json.city.json").read_text()))
    try:
        await s.city("load", import_path=args.source)
        assert s.last_state["population"] >= 10000
        assert s.last_state["debt"] == 0
        catalog = s.last_state["budget"]["unique_buildings"]
        assert sum(item["unlocked"] for item in catalog) >= 2
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.request("budget", {"action": "set_taxes", "tax_percent": s.last_state["budget"]["tax_percent"]})
        await s.request("budget", {"action": "close"})
        await keys("E")
        await s.screenshot("m52-earned-journey-catalog-minimum")
        await keys("Escape")
        reports = []
        placed = []
        for kind in (1, 6):
            session, core = await checkpoint()
            tiles = {(t["x"], t["y"]): t for t in core["tiles"]}
            sites = set()
            for f in session["facilities"]:
                span = 2 if f["type"] == 4 and f.get("powerSource") == 4 else 1
                sites.update((f["x"]+dx, f["y"]+dy) for dx in range(span) for dy in range(span))
            candidates = [p for p,t in tiles.items() if t["type"] != 2 and not t["hasRoad"] and
                not t["buildingId"] and not t["zone"] and p not in sites and
                t["connectedToPower"] and t["connectedToWater"] and
                all(abs(p[0]-q[0])+abs(p[1]-q[1]) >= 15 for q in placed) and
                any(tiles.get((p[0]+dx,p[1]+dy),{}).get("hasRoad",False)
                    for dx,dy in ((1,0),(-1,0),(0,1),(0,-1))) and
                any(t2["zone"] and abs(p[0]-q[0])+abs(p[1]-q[1]) <= 3 for q,t2 in tiles.items())]
            assert candidates, "No empty serviced site near zoned land"
            p = min(candidates, key=lambda p:(abs(p[0]-40)+abs(p[1]-48),p))
            placed.append(p)
            before = {p:t["landValue"] for p,t in tiles.items()}
            await keys("E")
            # The fresh catalog starts at kind 1; after the first build it stays there.
            if kind == 6:
                await keys("Up")
            await keys("Enter")
            await s.request("view", {"action": "projection", "projection": "top_down"})
            await s.request("view", {"action": "camera", "tile_pixels": 24,
                "view_x": max(0,p[0]-16), "view_y": max(0,p[1]-12)})
            screen = await s.request("coordinates", {"direction": "tile_to_screen", "x": p[0], "y": p[1]})
            cash = s.last_state["funds"]
            await s.request("input", {"events": [
                {"type": "move", **screen}, {"type": "down", "button": "left", **screen},
                {"type": "up", "button": "left", **screen}]})
            item = s.last_state["budget"]["unique_buildings"][kind-1]
            assert item["built"], (item,s.last_state["toast"])
            assert cash-s.last_state["funds"] == item["cost"]
            _, after = await checkpoint()
            changes = [t["landValue"]-before[(t["x"],t["y"])] for t in after["tiles"] if t["zone"] and
                abs(t["x"]-p[0])+abs(t["y"]-p[1]) < item["radius"]]
            assert changes and max(changes)>1, "Unique must measurably raise nearby land value"
            reports.append({"key":item["key"],"position":p,"cost":item["cost"],"max_local_value_increase":max(changes)})
            await s.screenshot("m52-journey-"+item["key"])
        saved, saved_core = await checkpoint()
        await s.request("time", {"action":"step", "ticks":6})
        future = {key:s.last_state[key] for key in ("funds","population","metrics","budget")}
        await s.city("load")
        again, _ = await checkpoint()
        assert saved == again, "Native load/save changed the session"
        await s.request("time", {"action":"step", "ticks":6})
        assert {key:s.last_state[key] for key in future} == future, "Continuation diverged"
        gui_session, gui_core = await checkpoint()
        cli = Path(args.executable).resolve().with_name("UrbanSimCore-cli")
        output = s.artifact("cli-out.json")
        # Rewind to the two-unique checkpoint for exact GUI/CLI parity.
        core_source=s.artifact("parity-source.city.json")
        core_source.write_text(json.dumps(saved_core))
        source = s.artifact("parity-source.json")
        source.write_text(json.dumps({**saved,"citySnapshot":str(core_source)}))
        with s.artifact("cli.log").open("wb") as log:
            process=await asyncio.create_subprocess_exec(str(cli),"--simulate","6",
                "--simulate-session",str(source),"--simulate-session-out",str(output),stdout=log,stderr=log)
            assert await process.wait() == 0, "CLI continuation failed"
        cli_session=json.loads(output.read_text())
        cli_core=json.loads(Path(cli_session["citySnapshot"]).read_text())
        gui_session.pop("citySnapshot");cli_session.pop("citySnapshot")
        assert gui_session == cli_session, "GUI/CLI sessions differ"
        for data in (gui_core,cli_core):
            for key in ("buildings","populationGroups"):
                data[key].sort(key=lambda item:item["id"])
        assert gui_core == cli_core, "GUI/CLI core cities differ"
        report = {"source":args.source,"buildings":reports,"continuation_ticks":6,
                  "gui_cli_parity":True,
                  "funds":s.last_state["funds"],"debt":s.last_state["debt"]}
        s.artifact("unique-report.json").write_text(json.dumps(report,indent=2))
        print(json.dumps({"directory":str(s.run_dir),"report":report},indent=2))
    finally:
        await s.stop()


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",required=True)
    parser.add_argument("--source",required=True)
    parser.add_argument("--artifacts",default="playtest-artifacts/m52/journey-native")
    asyncio.run(main(parser.parse_args()))
