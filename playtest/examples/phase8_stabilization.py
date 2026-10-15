"""Retained M30 growth/stress/recovery scenarios; never modifies source saves.

Run with the installed playtest package, --executable and --legacy-save.
Uses deterministic completed ticks, not a wall-clock endurance claim.
"""
import argparse
import asyncio
import json
import time
from pathlib import Path

from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record, replay, assert_state


def canonical(path):
    data = json.loads(path.read_text())
    snapshot = Path(data.pop("citySnapshot"))
    if not snapshot.is_absolute():
        snapshot = path.parent / snapshot
    city = json.loads(snapshot.read_text())
    # Stores use unordered containers; serialization order is not simulation state.
    for key in ("buildings", "populationGroups"):
        if key in city:
            city[key].sort(key=lambda item: item["id"])
    return {"session": data, "city": city}


async def run(args):
    root = Path(args.artifacts).resolve()
    root.mkdir(parents=True, exist_ok=True)
    summary = []
    for name in ("startup-growth", "legacy-mature"):
        s = Session(args.executable, root / name, timeout=120)
        samples = []
        try:
            await s.launch()
            await record(s, "start")
            if name == "legacy-mature":
                await s.city("load", import_path=args.legacy_save)
            else:
                await s.city("new", 32)
                await s.request("tool", {"action":"apply", "name":"road", "x1":4,"y1":16,"x2":27,"y2":16})
                for service, x in (("power",15),("water",17)):
                    await s.request("tool", {"action":"apply","name":"service","service":service,"x1":x,"y1":15})
                for zone,y in (("residential",15),("commercial",17),("industrial",18)):
                    await s.request("tool", {"action":"apply","name":"zone","zone":zone,"x1":4,"y1":y,"x2":12,"y2":y})
            initial_tick = s.last_state["tick"]
            initial_funds = s.last_state["funds"]
            async def sample(phase, ticks):
                start = time.perf_counter()
                await s.request("time", {"action":"step","ticks":ticks})
                elapsed = time.perf_counter()-start
                state = s.last_state.copy()
                samples.append({"phase":phase,"ticks":ticks,"seconds":elapsed,
                                "ms_per_tick":elapsed*1000/ticks,"state":state})
                print(name, phase, state["tick"], state["population"], round(elapsed,3), flush=True)
                (s.run_dir / "measurements.json").write_text(json.dumps(samples,indent=2))
            for _ in range(10):
                await sample("growth",100)
            await assert_state(s,"state.population","gt",0)
            normal = s.last_state["metrics"]["power_supply_mw"]
            funding = [100]*11
            funding[4]=0
            await s.request("budget",{"action":"set_funding","service_percent":funding})
            for _ in range(2):
                await sample("utility-stress",100)
            await assert_state(s,"state.metrics.power_supply_mw","eq",0)
            await assert_state(s,"state.budget.alerts.records.0.active","eq",True)
            capture = await s.screenshot("utility-stress")
            s.artifact(capture["capture"]).unlink()
            await s.request("budget",{"action":"reset_funding"})
            for _ in range(5):
                await sample("recovery",100)
            await assert_state(s,"state.metrics.power_supply_mw","eq",normal)
            await assert_state(s,"state.tick","eq",initial_tick+1700)
            capture = await s.screenshot("recovery")
            s.artifact(capture["capture"]).unlink()
            await s.city("save")
            before = canonical(s.artifact("urban_sim_session.json"))
            # Keep a stable checkpoint for both hosts without changing the original save.
            checkpoint = s.artifact("checkpoint.json")
            data = before["session"].copy()
            checkpoint_city = s.artifact("checkpoint.city.json")
            checkpoint_city.write_text(json.dumps(before["city"]))
            data["citySnapshot"] = str(checkpoint_city)
            checkpoint.write_text(json.dumps(data))
            await s.city("load")
            await s.city("save")
            assert canonical(s.artifact("urban_sim_session.json")) == before, "save/load changed persisted state"
            await sample("continuation",20)
            await s.city("save")
            gui = canonical(s.artifact("urban_sim_session.json"))
            output = s.artifact("headless.json")
            cli = Path(args.executable).resolve().with_name("UrbanSimCore-cli")
            proc = await asyncio.create_subprocess_exec(str(cli),"--simulate","20",
                "--simulate-session",str(checkpoint),"--simulate-session-out",str(output),
                stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
            stdout,stderr = await proc.communicate()
            assert proc.returncode == 0, stderr.decode()
            assert canonical(output) == gui, "GUI/headless continuation differs"
            scenario = await record(s,"stop",name)
            # Absolute paths in the checkpoint are run-local; replay imports are embedded.
            result = await replay(s,scenario["scenario"],2)
            assert result["identical"], "replay differed"
            summary.append({"name":name,"initial_tick":initial_tick,"initial_funds":initial_funds,
                "measurements":samples,"continuation_identical":True,"replay":result})
            (root / "results.json").write_text(json.dumps(summary,indent=2))
        finally:
            await s.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",required=True)
    parser.add_argument("--legacy-save",required=True)
    parser.add_argument("--artifacts",required=True)
    asyncio.run(run(parser.parse_args()))
