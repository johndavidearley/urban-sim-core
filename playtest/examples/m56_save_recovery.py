"""Native production-store save, autosave, corruption recovery and restart.

Uses the earned M55 city unchanged. Fault injection truncates files only in the
isolated run directory; it never edits funds or simulation fields.
"""
import argparse
import asyncio
import hashlib
import json
import os
import shutil
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record


async def run(args):
    sessions = [Session(args.executable, args.artifacts, timeout=120) for _ in range(2)]
    first, second = sessions
    runner = Path(__file__).read_bytes()
    result = {"executable_sha256": hashlib.sha256(Path(args.executable).read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(runner).hexdigest(), "faults": []}

    def damage(path, contents):
        path.write_text(contents)
        result["faults"].append({"path": str(path), "contents": contents})

    async def launch(session):
        previous = os.environ.get("URBAN_SIM_PROTECTED_SAVES")
        os.environ["URBAN_SIM_PROTECTED_SAVES"] = "1"
        try:
            await session.launch()
        finally:
            if previous is None:
                os.environ.pop("URBAN_SIM_PROTECTED_SAVES", None)
            else:
                os.environ["URBAN_SIM_PROTECTED_SAVES"] = previous
        await record(session, "start")

    async def keys(session, *names):
        await session.request("input", {"events": [{"type": "key", "key": n} for n in names]})

    def generation(root, slot):
        return root / "objects" / json.loads((root / (slot + ".json")).read_text())["generation"] / "session.json"

    try:
        await launch(first)
        first.artifact("runner.py").write_bytes(runner)
        first.import_save(args.session)
        await keys(first, "L")
        base = {k: first.last_state[k] for k in ("tick", "funds", "population")}
        root = first.run_dir / "player-saves"
        await keys(first, "F5")
        assert generation(root, "City").is_file()
        await keys(first, "N", "F5")
        assert (root / "City.backup1.json").is_file()
        for _ in range(2):
            await keys(first, *(["N"] * 100))
        assert (root / "Autosave.json").is_file()
        autosave = json.loads(generation(root, "Autosave").read_text())
        assert autosave["tick"] >= base["tick"] + 200
        await keys(first, "F11")
        info = await first.request("inspect", {"section": "ui"})
        index = next(i for i, row in enumerate(info["save_rows"]) if row["name"] == "Autosave" and row["backup"] == 0)
        assert index < 7
        for label in ("saves_row_" + str(index), "saves_load", "saves_load"):
            if not first.last_state["ui"]["save_browser"]:
                break
            controls = (await first.request("inspect", {"section": "ui"}))["controls"]
            control = next(c for c in controls if c["name"] == label)
            x, y = control["x"] + control["width"] // 2, control["y"] + control["height"] // 2
            await first.request("input", {"events": [
                {"type": "down", "button": "left", "x": x, "y": y},
                {"type": "up", "button": "left", "x": x, "y": y}]})
        assert not first.last_state["ui"]["save_browser"]
        assert first.last_state["tick"] == autosave["tick"]
        result["autosave_browser_selection"] = True
        # Corrupt the current city half; F9 must choose a complete older pair.
        damage(generation(root, "City").with_name("session.json.city.json"), "{interrupted")
        await keys(first, "F9")
        assert first.last_state["tick"] == base["tick"]
        assert first.last_state["funds"] == base["funds"]
        assert "RECOVERED" in first.last_state["toast"].upper()
        result["backup_recovery"] = True
        # With both manual generations damaged, use the independent autosave.
        damage(generation(root, "City.backup1"), "{invalid")
        await keys(first, "F9")
        assert first.last_state["tick"] == autosave["tick"]
        assert first.last_state["funds"] == autosave["funds"]
        assert "AUTOSAVE" in first.last_state["toast"].upper()
        await first.screenshot("m56-autosave-recovered")
        await keys(first, "F5")
        earned = {k: first.last_state[k] for k in ("tick", "funds", "population")}
        # Native save-and-quit must finish publication before closing the process.
        await first.request("window", {"action": "close"})
        controls = (await first.request("inspect", {"section": "ui"}))["controls"]
        c = next(c for c in controls if c["name"] == "save_and_quit")
        x, y = c["x"] + c["width"] // 2, c["y"] + c["height"] // 2
        await first.request("input", {"events": [
            {"type": "down", "button": "left", "x": x, "y": y},
            {"type": "up", "button": "left", "x": x, "y": y}]})
        first.process.stdin.close()
        assert await asyncio.wait_for(first.process.wait(), 10) == 0
        await record(first, "stop", "m56-protected-save-recovery")
        await launch(second)
        shutil.copytree(root, second.run_dir / "player-saves")
        await keys(second, "L")
        assert {k: second.last_state[k] for k in earned} == earned
        await keys(second, "N")
        assert second.last_state["tick"] == earned["tick"] + 1
        await second.screenshot("m56-protected-city-restarted")
        before = {k: second.last_state[k] for k in ("tick", "funds", "population", "buildings")}
        for slot in (second.run_dir / "player-saves").glob("*.json"):
            damage(slot, "{invalid manifest")
        await keys(second, "F9")
        assert {k: second.last_state[k] for k in before} == before
        assert "CANNOT LOAD" in second.last_state["toast"].upper()
        await second.screenshot("m56-invalid-load-preserves-city")
        await keys(second, "F5")
        await record(second, "stop", "m56-protected-restart")
        result.update(complete=True, autosave_recovery=True, restarted=True, invalid_load_preserves_city=True,
                      first_run=str(first.run_dir), second_run=str(second.run_dir),
                      resumed_tick=second.last_state["tick"], population=second.last_state["population"])
        first.artifact("save-recovery-result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2), flush=True)
    finally:
        for session in sessions:
            await session.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="build-strict/bin/UrbanSimCore-playtest")
    parser.add_argument("--artifacts", default="playtest-artifacts/m56-save-recovery")
    parser.add_argument("--session", required=True)
    asyncio.run(run(parser.parse_args()))
