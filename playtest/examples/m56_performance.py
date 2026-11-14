"""Measure real present intervals with the earned city running at native 1X.

The opt-in application CSV includes simulation/rendering stalls. This is a
developer-build hardware probe, not packaged-soak or minimum-hardware sign-off.
"""
import argparse
import asyncio
import csv
import hashlib
import json
import math
import os
import platform
import time
from pathlib import Path

from urban_sim_mcp.session import Session


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


async def run(args):
    root = Path(args.artifacts).resolve()
    root.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    capture = root / (stamp + "-frames.csv")
    previous = os.environ.get("URBAN_SIM_PERFORMANCE_CSV")
    os.environ["URBAN_SIM_PERFORMANCE_CSV"] = str(capture)
    session = Session(args.executable, root, timeout=120)

    async def key(name):
        await session.request("input", {"events": [{"type": "key", "key": name}]})

    async def control(name):
        controls = (await session.request("inspect", {"section": "ui"}))["controls"]
        c = next(c for c in controls if c["name"] == name)
        x, y = c["x"] + c["width"] // 2, c["y"] + c["height"] // 2
        await session.request("input", {"events": [
            {"type": "down", "button": "left", "x": x, "y": y},
            {"type": "up", "button": "left", "x": x, "y": y}]})

    try:
        await session.launch(mode="realtime")
        session.import_save(args.session)
        await key("L")
        assert session.last_state["population"] >= 10000
        await key("F10")
        await control("settings_reset")
        await control("settings_resolution")
        await control("settings_resolution")
        await control("settings_apply")
        await control("settings_close")
        assert [session.last_state["dimensions"][k] for k in ("width", "height")] == [1920, 1080]
        await key("F")
        await control("speed_1")  # The native 1X button is a 700 ms interval.
        if session.last_state["paused"]:
            await key("Space")
        await asyncio.sleep(args.warmup)
        await session.request("status")
        start_tick = session.last_state["tick"]
        started = time.monotonic()
        # No status polling during the measurement: bridge serialization itself
        # traverses the city and would bias a gameplay frame-time sample.
        await asyncio.sleep(args.seconds)
        ended = time.monotonic()
        await session.request("status")
        end_tick = session.last_state["tick"]
        state = session.last_state
        await key("Space")
        await session.screenshot("m56-performance-city")
        await session.stop()
        rows = list(csv.DictReader(capture.open(newline="", encoding="utf-8")))
        active = [r for r in rows if r["paused"] == "0"
                  and start_tick < int(r["tick"]) < end_tick
                  and r["width"] == "1920" and r["height"] == "1080"]
        assert active, "No active 1080p samples"
        frames = [float(r["frame_ms"]) for r in active]
        ticks = [float(r["tick_ms"]) for r in active if float(r["tick_ms"]) > 0]
        interval = state["speed_interval_ms"]
        wall = ended - started
        result = {
            "platform": platform.platform(), "run": str(session.run_dir),
            "executable_sha256": hashlib.sha256(Path(args.executable).read_bytes()).hexdigest(),
            "checkpoint_sha256": hashlib.sha256(Path(args.session).read_bytes()).hexdigest(),
            "frame_csv": str(capture), "sample_seconds": wall,
            "frames": len(frames), "mean_fps": 1000 / (sum(frames) / len(frames)),
            "p50_frame_ms": percentile(frames, .5), "p95_frame_ms": percentile(frames, .95),
            "p99_frame_ms": percentile(frames, .99), "max_frame_ms": max(frames),
            "p95_tick_ms": percentile(ticks, .95) if ticks else None,
            "max_schedule_late_ms": max(float(r["schedule_late_ms"]) for r in active),
            "start_tick": start_tick, "end_tick": end_tick,
            "interval_ms": interval, "expected_ticks": wall * 1000 / interval,
            "completed_ticks": end_tick - start_tick,
            "tick_deficit": max(0, wall * 1000 / interval - (end_tick - start_tick)),
            "population": state["population"], "validation_errors": state["validation_errors"],
            "frame_time_pass": percentile(frames, .95) <= 33.3,
            "scope": "developer realtime 1080p probe; no packaged-soak or minimum-hardware sign-off",
        }
        session.artifact("performance-result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2), flush=True)
    finally:
        await session.stop()
        if previous is None:
            os.environ.pop("URBAN_SIM_PERFORMANCE_CSV", None)
        else:
            os.environ["URBAN_SIM_PERFORMANCE_CSV"] = previous


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable", required=True)
    p.add_argument("--session", required=True)
    p.add_argument("--artifacts", default="build-windows/m56-performance")
    p.add_argument("--seconds", type=float, default=120)
    p.add_argument("--warmup", type=float, default=10)
    args = p.parse_args()
    if args.seconds <= 0 or args.warmup < 0:
        p.error("seconds must be positive and warmup nonnegative")
    asyncio.run(run(args))
