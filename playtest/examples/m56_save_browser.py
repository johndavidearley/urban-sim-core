"""Native named saves, confirmations, backup selection and start-screen loading.

Protected stores live in isolated run directories. The optional 10k checkpoint
is loaded unchanged to verify selection across map sizes. No funds are granted.
"""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import shutil
from urban_sim_mcp.session import Session, PlaytestError
from urban_sim_mcp.scenarios import record


async def run(args):
    sessions = []
    runner = Path(__file__).read_bytes()
    result = {"executable_sha256": hashlib.sha256(Path(args.executable).read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(runner).hexdigest(), "runs": []}

    async def launch(mode="deterministic"):
        session = Session(args.executable, args.artifacts, timeout=120)
        sessions.append(session)
        previous = os.environ.get("URBAN_SIM_PROTECTED_SAVES")
        os.environ["URBAN_SIM_PROTECTED_SAVES"] = "1"
        try:
            await session.launch(mode=mode)
        finally:
            if previous is None:
                os.environ.pop("URBAN_SIM_PROTECTED_SAVES", None)
            else:
                os.environ["URBAN_SIM_PROTECTED_SAVES"] = previous
        result["runs"].append(str(session.run_dir))
        session.artifact("runner.py").write_bytes(runner)
        await record(session, "start")
        return session

    async def keys(session, *names):
        await session.request("input", {"events": [{"type": "key", "key": n} for n in names]})

    async def click(session, name):
        info = await session.request("inspect", {"section": "ui"})
        control = next(c for c in info["controls"] if c["name"] == name)
        x, y = control["x"] + control["width"] // 2, control["y"] + control["height"] // 2
        dims = session.last_state["dimensions"]
        assert 0 <= x < dims["width"] and 0 <= y < dims["height"], control
        await session.request("input", {"events": [
            {"type": "down", "button": "left", "x": x, "y": y},
            {"type": "up", "button": "left", "x": x, "y": y}]})

    async def name(session, value):
        await click(session, "saves_clear")
        if value:
            await session.request("input", {"events": [{"type": "text", "text": value}]})

    async def select(session, label, backup=0):
        info = await session.request("inspect", {"section": "ui"})
        index = next(i for i, r in enumerate(info["save_rows"]) if r["name"] == label and r["backup"] == backup)
        page = info.get("save_browser_page", session.last_state.get("ui", {}).get("save_browser_page", 0))
        while page != index // 7:
            await click(session, "saves_next" if page < index // 7 else "saves_previous")
            page += 1 if page < index // 7 else -1
        await click(session, "saves_row_" + str(index % 7))

    def manifest(session, label):
        return session.run_dir / "player-saves" / (label + ".json")

    def current(session, label):
        root = session.run_dir / "player-saves"
        return root / "objects" / json.loads(manifest(session, label).read_text())["generation"] / "session.json"

    try:
        first = await launch()
        await first.city("new", 32)
        await click(first, "saves")
        before = {k: first.last_state[k] for k in ("tick", "funds", "population")}
        await keys(first, "N", "R", "W", "F5")
        assert {k: first.last_state[k] for k in before} == before
        try:
            await first.request("time", {"action": "step", "ticks": 1})
            raise AssertionError("Modal semantic stepping was accepted")
        except PlaytestError as exc:
            assert "Close save browser" in str(exc)
        await name(first, "Harbor BayZ")
        await keys(first, "Backspace")
        assert first.last_state["ui"]["save_browser_name"] == "Harbor Bay"
        # Expanded text batches reject atomically before any events are queued.
        try:
            await first.request("input", {"events": [{"type": "text", "text": "a" * 48}] * 6})
            raise AssertionError("Oversized expanded text batch was accepted")
        except PlaytestError as exc:
            assert "256" in str(exc)
        assert first.last_state["ui"]["save_browser_name"] == "Harbor Bay"
        await click(first, "saves_save")
        assert manifest(first, "Harbor Bay").is_file()
        await click(first, "saves_close")
        await keys(first, "N", "F5")
        assert json.loads(current(first, "Harbor Bay").read_text())["tick"] == 1
        harbor = manifest(first, "Harbor Bay").read_bytes()
        await keys(first, "F11")
        for invalid in ("", "Autosave", "CON"):
            await name(first, invalid)
            await click(first, "saves_save")
            assert "valid name" in first.last_state["ui"]["save_browser_status"]
            assert not manifest(first, invalid).exists()
        await name(first, "Hill Town")
        await click(first, "saves_save")
        hill = manifest(first, "Hill Town").read_bytes()
        await click(first, "saves_save")
        assert manifest(first, "Hill Town").read_bytes() == hill
        assert "again" in first.last_state["ui"]["save_browser_status"]
        await click(first, "saves_save")
        assert manifest(first, "Hill Town").read_bytes() != hill
        assert manifest(first, "Harbor Bay").read_bytes() == harbor
        await click(first, "saves_close")
        await keys(first, "N", "F11")
        await select(first, "Harbor Bay", 1)
        await click(first, "saves_load")
        assert first.last_state["tick"] == 2
        assert "Unsaved changes" in first.last_state["ui"]["save_browser_status"]
        await first.screenshot("m56-confirm-backup-load")
        await click(first, "saves_load")
        assert first.last_state["tick"] == 0
        assert first.last_state["ui"]["save_name"] == "Harbor Bay"
        await keys(first, "N", "F5", "F11")
        assert json.loads(current(first, "Harbor Bay").read_text())["tick"] == 1
        backup = current(first, "Harbor Bay.backup1")
        backup.write_text("{invalid")
        await click(first, "saves_refresh")
        await select(first, "Harbor Bay", 1)
        await click(first, "saves_load")
        assert first.last_state["tick"] == 1
        assert "unavailable" in first.last_state["ui"]["save_browser_status"]
        for i in range(8):
            await name(first, f"Z Town {i}")
            await click(first, "saves_save")
        await keys(first, "PageUp")
        assert first.last_state["ui"]["save_browser_page"] == 0
        await click(first, "saves_next")
        assert first.last_state["ui"]["save_browser_page"] == 1
        for width, height in ((1280, 720), (800, 640)):
            await first.request("window", {"action": "resize", "width": width, "height": height})
            await first.screenshot(f"m56-save-browser-{width}x{height}")
        await record(first, "stop", "m56-named-browser")
        root = first.run_dir / "player-saves"
        await first.stop()
        result.update(named_saves=True, overwrite_confirmation=True, backup_selection=True,
                      unsaved_load_confirmation=True, invalid_generation_preserves_city=True, pagination=True)

        if args.session:
            second = await launch()
            shutil.copytree(root, second.run_dir / "player-saves")
            second.import_save(args.session)
            await keys(second, "L", "F11")
            assert second.last_state["map_size"] == 96
            await name(second, "Metropolis")
            await click(second, "saves_save")
            await select(second, "Harbor Bay")
            await click(second, "saves_load")
            assert second.last_state["map_size"] == 96
            assert "Different map size" in second.last_state["ui"]["save_browser_status"]
            await second.screenshot("m56-other-map-preserved")
            await record(second, "stop", "m56-other-map")
            root = second.run_dir / "player-saves"
            await second.stop()
            for label, size in (("Metropolis", 96), ("Harbor Bay", 32)):
                resumed = await launch()
                shutil.copytree(root, resumed.run_dir / "player-saves")
                await click(resumed, "saves")
                await select(resumed, label)
                await resumed.screenshot(f"m56-start-screen-{size}-selected")
                await click(resumed, "saves_load")
                assert resumed.last_state["map_size"] == size
                assert resumed.last_state["ui"]["save_name"] == label
                if size == 96:
                    assert resumed.last_state["population"] >= 10000
                await keys(resumed, "N", "F5")
                assert json.loads(current(resumed, label).read_text())["tick"] == resumed.last_state["tick"]
                await resumed.screenshot(f"m56-named-{size}-resumed")
                await record(resumed, "stop", f"m56-named-{size}-restart")
                await resumed.stop()
            result["start_screen_map_sizes"] = [96, 32]

        realtime = await launch("realtime")
        await realtime.city("new", 32)
        await keys(realtime, "F11")
        before = {k: realtime.last_state[k] for k in ("tick", "funds")}
        await asyncio.sleep(1)
        await realtime.request("status")
        assert {k: realtime.last_state[k] for k in before} == before
        await record(realtime, "stop", "m56-browser-realtime-isolation")
        result.update(complete=True, realtime_isolation=True)
        first.artifact("save-browser-result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2), flush=True)
    finally:
        for session in sessions:
            await session.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="build-strict/bin/UrbanSimCore-playtest")
    parser.add_argument("--artifacts", default="playtest-artifacts/m56-save-browser")
    parser.add_argument("--session", help="Optional earned M55 96x96 checkpoint")
    asyncio.run(run(parser.parse_args()))
