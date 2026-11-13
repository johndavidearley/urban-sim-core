"""Capture M55 display evidence on a real desktop; never substitute requested sizes.

Run on Windows at 150% desktop scaling with --windows-150. Screenshots still
require a human readability review. --allow-constrained retains useful local
captures without claiming the full resolution gate passed.
"""
import argparse
import asyncio
import hashlib
import json
import platform
from pathlib import Path

from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record


def window_dpi(pid):
    """Query the game window itself, rather than the Python process's DPI."""
    if platform.system() != "Windows":
        return None
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetDpiForWindow.argtypes = [wintypes.HWND]
    user32.GetDpiForWindow.restype = wintypes.UINT
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    values = []

    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            values.append(int(user32.GetDpiForWindow(hwnd)))
        return True

    if not user32.EnumWindows(visit, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    if len(values) != 1 or values[0] == 0:
        raise RuntimeError(f"Expected one visible game window, got DPI values {values}")
    return values[0]


async def check(args):
    session = Session(args.executable, args.artifacts, timeout=120)
    result = {
        "platform": platform.platform(),
        "executable_sha256": hashlib.sha256(Path(args.executable).read_bytes()).hexdigest(),
        "windows_150_requested": args.windows_150,
        "manual_readability_review_required": True,
        "cases": [],
    }

    async def key(name):
        await session.request("input", {"events": [{"type": "key", "key": name}]})

    async def click(name):
        controls = (await session.request("inspect", {"section": "ui"}))["controls"]
        control = next(c for c in controls if c["name"] == name)
        x, y = control["x"] + control["width"] // 2, control["y"] + control["height"] // 2
        width, height = (session.last_state["dimensions"][k] for k in ("width", "height"))
        assert 0 <= x < width and 0 <= y < height, (name, control)
        await session.request("input", {"events": [
            {"type": "down", "button": "left", "x": x, "y": y},
            {"type": "up", "button": "left", "x": x, "y": y},
        ]})

    async def capture(label, requested=None):
        await session.request("status")
        state = session.last_state
        case = {"label": label, "actual": dict(state["dimensions"]),
                "window_dpi": window_dpi(session.process.pid),
                "validation_errors": state["validation_errors"]}
        if requested:
            case["requested"] = requested
            case["size_matches"] = [case["actual"][k] for k in ("width", "height")] == requested
        assert case["validation_errors"] == 0, case
        await session.screenshot(label)
        result["cases"].append(case)

    try:
        await session.launch()
        await record(session, "start")
        await capture("start-screen")
        if args.session:
            session.import_save(args.session)
            await key("L")
        else:
            await click("new")
        for index, size in ((0, [1280, 720]), (2, [1920, 1080])):
            await key("F10")
            await click("settings_reset")
            for _ in range(index):
                await click("settings_resolution")
            await click("settings_apply")
            tick, funds = (session.last_state[k] for k in ("tick", "funds"))
            await key("N")
            assert (session.last_state["tick"], session.last_state["funds"]) == (tick, funds)
            label = f"{size[0]}x{size[1]}"
            await capture(label + "-settings", size)
            await click("settings_help")
            await capture(label + "-controls", size)
            await click("settings_help")
            await click("settings_text")
            await click("settings_apply")
            await click("settings_close")
            await capture(label + "-compact-city", size)
            await key("F10")
            await click("settings_text")
            await click("settings_apply")
            await click("settings_close")
            await capture(label + "-readable-city", size)
            await key("F6")
            await capture(label + "-budget", size)
            await key("Escape")
        await key("F10")
        await click("settings_display")
        await click("settings_apply")
        assert session.last_state["ui"]["settings_borderless"]
        await capture("borderless-settings")
        await click("settings_close")
        await capture("borderless-city")
        await key("F10")
        await click("settings_reset")
        await click("settings_apply")
        await click("settings_close")
        result["exact_resolutions_pass"] = all(c.get("size_matches", True) for c in result["cases"])
        result["windows_150_pass"] = platform.system() == "Windows" and all(
            c["window_dpi"] == 144 for c in result["cases"])
        result["automated_checks_pass"] = result["exact_resolutions_pass"] and (
            not args.windows_150 or result["windows_150_pass"])
        await record(session, "stop", "m55-display-check")
        session.artifact("display-result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps({"run": str(session.run_dir), **result}, indent=2), flush=True)
        if not result["automated_checks_pass"] and not args.allow_constrained:
            raise RuntimeError("Desktop dimensions or DPI do not meet the requested gate; see display-result.json")
    finally:
        await session.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", default="build-strict/bin/UrbanSimCore-playtest")
    parser.add_argument("--artifacts", default="playtest-artifacts/m55-display")
    parser.add_argument("--session", help="Optional earned city checkpoint to inspect")
    parser.add_argument("--windows-150", action="store_true", help="Require Windows game-window DPI 144")
    parser.add_argument("--allow-constrained", action="store_true", help="Capture unsupported desktops without passing the gate")
    asyncio.run(check(parser.parse_args()))
