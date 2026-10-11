"""Recorded scenarios use declarative assertions, never executable expressions."""

from __future__ import annotations
import argparse
import asyncio
import anyio
import hashlib
import json
from pathlib import Path
from typing import Any
from .session import PlaytestError, Session


def evaluate_assertion(document: dict, field: str, op: str, expected: Any) -> dict:
    current: Any = document
    try:
        for component in field.split("."):
            current = (
                current[int(component)]
                if isinstance(current, list)
                else current[component]
            )
    except (KeyError, IndexError, ValueError, TypeError) as error:
        raise PlaytestError(f"Unknown assertion field: {field}") from error
    if op == "count":
        if not isinstance(current, (list, dict, str)):
            raise PlaytestError("Count assertion requires a collection")
        passed = len(current) == expected
    elif op == "eq":
        passed = current == expected
    elif op in ("lt", "le", "gt", "ge"):
        if type(current) not in (int, float) or type(expected) not in (int, float):
            raise PlaytestError("Numeric assertion requires numbers")
        passed = {
            "lt": lambda: current < expected,
            "le": lambda: current <= expected,
            "gt": lambda: current > expected,
            "ge": lambda: current >= expected,
        }[op]()
    else:
        raise PlaytestError("Assertion operator must be eq, count, lt, le, gt, or ge")
    result = dict(field=field, op=op, expected=expected, actual=current, passed=passed)
    if not passed:
        raise PlaytestError("Assertion failed: " + json.dumps(result))
    return result


async def assert_state(
    session: Session,
    field: str,
    op: str,
    expected: Any,
    section="summary",
    query: dict | None = None,
):
    async with session.lock:
        document = await session._request(
            "inspect", {"section": section, **(query or {})}
        )
        assertion = dict(
            field=field, op=op, expected=expected, section=section, query=query or {}
        )
        # The inspection is useful evidence but replay needs the assertion itself.
        if session.recording is not None:
            session.recording["steps"].pop()
            session.recording["steps"].append({"assert": assertion})
        try:
            result = evaluate_assertion(document, field, op, expected)
            session.trace({"assertion": assertion, "result": result})
            return result
        except PlaytestError as error:
            session.trace({"assertion": assertion, "failure": str(error)})
            raise


async def record(session: Session, action: str, name="scenario"):
    async with session.lock:
        if action == "start":
            if session.recording is not None:
                raise PlaytestError("Recording already active")
            if session.last_state.get("phase") != "start_screen":
                raise PlaytestError(
                    "Start recording from a freshly launched start screen"
                )
            session.recording = {
                "version": 1,
                "configuration": session.config.copy(),
                "capabilities": session.capabilities,
                "steps": [],
            }
            import time

            session.record_started = time.monotonic()
            return {"recording": True}
        if action != "stop" or session.recording is None:
            raise PlaytestError("No recording active or unknown recording action")
        if (
            not name
            or len(name) > 128
            or any(
                c
                not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."
                for c in name
            )
        ):
            raise PlaytestError("Scenario name must be a simple filename")
        path = session.artifact("scenarios/" + name + ".json")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(session.recording, indent=2), encoding="utf-8")
        steps = len(session.recording["steps"])
        session.recording = None
        return {"recording": False, "scenario": str(path), "steps": steps}


async def replay(session: Session, path: str, repeats=1):
    if repeats not in (1, 2):
        raise PlaytestError("Replay repeats must be 1 or 2")
    scenario = json.loads(Path(path).read_text(encoding="utf-8"))
    if (
        scenario.get("version") != 1
        or not isinstance(scenario.get("steps"), list)
        or len(scenario["steps"]) > 10000
    ):
        raise PlaytestError("Expected version 1 scenario with at most 10000 steps")
    allowed = {
        "input",
        "window",
        "city",
        "time",
        "tool",
        "view",
        "inspect",
        "coordinates",
        "screenshot",
        "diagnostics",
        "status",
    }
    for step in scenario["steps"]:
        if "assert" not in step and step.get("command") not in allowed:
            raise PlaytestError("Unsupported recorded command")
        if not 0 <= step.get("delay_ms", 0) <= 60000:
            raise PlaytestError("Scenario delay must be 0..60000 ms")
    if (
        repeats == 2
        and scenario.get("configuration", {}).get("mode", "deterministic")
        != "deterministic"
    ):
        raise PlaytestError("Two-run state comparison requires deterministic mode")
    if repeats == 2 and any(
        step.get("command") == "time"
        and step.get("args", {}).get("action") == "mode"
        and step.get("args", {}).get("mode") == "realtime"
        for step in scenario["steps"]
    ):
        raise PlaytestError("Two-run comparison cannot include realtime mode")
    # Keep the entire replay exclusive so a concurrent client cannot interleave edits.
    async with session.lock:
        reports = []
        for _ in range(repeats):
            await session._terminate()
            await session._launch(**scenario.get("configuration", {}))
            report = {
                "run_directory": str(session.run_dir),
                "passed": False,
                "steps": 0,
            }
            try:
                for index, step in enumerate(scenario["steps"]):
                    if not session.last_state.get("deterministic", True):
                        await asyncio.sleep(step.get("delay_ms", 0) / 1000)
                    if "assert" in step:
                        assertion = step["assert"]
                        document = await session._request(
                            "inspect",
                            {
                                "section": assertion.get("section", "summary"),
                                **assertion.get("query", {}),
                            },
                        )
                        evaluate_assertion(
                            document,
                            assertion["field"],
                            assertion["op"],
                            assertion["expected"],
                        )
                    else:
                        if "import_save" in step:
                            imported = step["import_save"]
                            saved = dict(imported["session"])
                            saved["citySnapshot"] = "urban_sim_session.json.city.json"
                            session.artifact("urban_sim_session.json").write_text(
                                json.dumps(saved), encoding="utf-8"
                            )
                            session.artifact(
                                "urban_sim_session.json.city.json"
                            ).write_text(json.dumps(imported["city"]), encoding="utf-8")
                        try:
                            result = await session._request(
                                step["command"], step.get("args", {})
                            )
                        except PlaytestError as error:
                            if step.get("expected_error") != str(error):
                                raise
                            report["steps"] = index + 1
                            continue
                        if "expected_error" in step:
                            raise PlaytestError(
                                "Recorded failure unexpectedly succeeded"
                            )
                        if step["command"] == "screenshot":
                            from PIL import Image

                            with Image.open(
                                session.artifact(result["capture"])
                            ) as image:
                                image.save(
                                    session.artifact(result["capture"]).with_suffix(
                                        ".png"
                                    )
                                )
                    report["steps"] = index + 1
                if session.last_state.get("phase") in ("gameplay", "quit_dialog"):
                    await session._request("city", {"action": "save"})
                    saved = json.loads(
                        session.artifact("urban_sim_session.json").read_text()
                    )
                    city = json.loads(
                        session.artifact("urban_sim_session.json.city.json").read_text()
                    )
                    saved.pop("citySnapshot", None)
                    canonical = json.dumps(
                        {"session": saved, "city": city},
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    report["canonical_hash"] = hashlib.sha256(
                        canonical.encode()
                    ).hexdigest()
                report["passed"] = True
            except BaseException as error:
                report["error"] = type(error).__name__ + ": " + str(error)
                session.artifact("replay.json").write_text(json.dumps(report, indent=2))
                if isinstance(error, asyncio.CancelledError):
                    with anyio.CancelScope(shield=True):
                        await session._terminate()
                    raise
                raise PlaytestError(
                    f"Replay failed; evidence at {session.run_dir}: {error}"
                ) from error
            session.artifact("replay.json").write_text(json.dumps(report, indent=2))
            reports.append(report)
        if repeats == 2:
            if session.config["mode"] != "deterministic":
                raise PlaytestError(
                    "Two-run state comparison requires deterministic mode"
                )
            if not reports[0].get("canonical_hash") or reports[0][
                "canonical_hash"
            ] != reports[1].get("canonical_hash"):
                raise PlaytestError("Canonical city state differs between replays")
        return {"passed": True, "runs": reports, "identical": repeats == 2}


def main():
    parser = argparse.ArgumentParser(
        description="Replay an UrbanSimCore playtest scenario"
    )
    parser.add_argument("scenario")
    parser.add_argument("--executable", required=True)
    parser.add_argument("--artifacts", default="playtest-artifacts")
    parser.add_argument("--repeats", type=int, choices=(1, 2), default=1)
    args = parser.parse_args()

    async def run():
        session = Session(args.executable, args.artifacts)
        try:
            print(
                json.dumps(await replay(session, args.scenario, args.repeats), indent=2)
            )
        finally:
            await session._terminate()

    asyncio.run(run())


if __name__ == "__main__":
    main()
