"""Synthetic same-tick/unchanged-topology reload regression for visual activity.

Only the owned copy of a save is edited; the player source save is untouched.
The separate organic walkthrough covers paced player usage.
"""
import argparse
import asyncio
import json
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.paced_input import PacedInput


async def run(args):
    s = Session(args.executable, args.artifacts, timeout=120)
    player = PacedInput(s)
    try:
        await s.launch(mode="realtime")
        s.import_save(args.session)
        await player.key("L")
        if not s.last_state["paused"]:
            await player.key("Space")
        await player.pause(.3)
        await s.request("status")
        before = {k:s.last_state[k] for k in ("tick","road_tiles","buildings","population")}
        assert s.last_state["activity"]["pedestrians"] > 0
        session = json.loads(s.artifact("urban_sim_session.json").read_text())
        snapshot = json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        for building in snapshot["buildings"]:
            building["occupancy"] = 0
        for group in snapshot["populationGroups"]:
            group["size"] = group["employed"] = 0
        empty_core = s.artifact("unoccupied.city.json")
        empty_core.write_text(json.dumps(snapshot))
        session["citySnapshot"] = str(empty_core)
        empty_session = s.artifact("unoccupied.json")
        empty_session.write_text(json.dumps(session))
        s.import_save(empty_session)
        await player.key("F9")
        await player.pause(.3)
        await s.request("status")
        for key in ("tick","road_tiles","buildings"):
            assert s.last_state[key] == before[key], key
        assert s.last_state["population"] == 0
        assert s.last_state["activity"]["pedestrians"] == 0, "Reload retained stale pedestrians"
        s.import_save(args.session)
        await player.key("F9")
        await player.pause(.3)
        await s.request("status")
        assert s.last_state["population"] == before["population"]
        assert s.last_state["activity"]["pedestrians"] > 0
        assert s.last_state["validation_errors"] == 0
        s.artifact("reload-result.json").write_text(json.dumps({
            "same_tick_topology_building_count":True,"empty_occupancy_clears_pedestrians":True,
            "restored_occupancy_restores_pedestrians":True,"validation_errors":0},indent=2))
        print(s.run_dir, flush=True)
    finally:
        await s.stop()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable",required=True)
    p.add_argument("--session",required=True)
    p.add_argument("--artifacts",default="playtest-artifacts/city-activity-reload")
    asyncio.run(run(p.parse_args()))
