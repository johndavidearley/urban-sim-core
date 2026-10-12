"""Set URBAN_SIM_PLAYTEST_EXECUTABLE to enable real Vulkan/GLFW acceptance tests."""

import asyncio
import base64
import io
import json
import os
import sys
import unittest
from pathlib import Path
from PIL import Image
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from urban_sim_mcp.session import Session, PlaytestError
from urban_sim_mcp.scenarios import record, assert_state, replay

EXECUTABLE = os.environ.get("URBAN_SIM_PLAYTEST_EXECUTABLE")
ARTIFACTS = os.environ.get("URBAN_SIM_PLAYTEST_ARTIFACTS", "build/playtest-acceptance")


@unittest.skipUnless(EXECUTABLE, "Rendered tests require URBAN_SIM_PLAYTEST_EXECUTABLE")
class Rendered(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.session = Session(EXECUTABLE, ARTIFACTS)
        await self.session.launch()

    async def asyncTearDown(self):
        await self.session.stop()

    async def test_semantic_scenario_and_identical_replays(self):
        s = self.session
        await record(s, "start")
        await s.city("new", 32)
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request(
            "view", {"action": "toggle", "name": "compact_ui", "enabled": True}
        )
        await s.request(
            "tool",
            {"action": "apply", "name": "road", "x1": 4, "y1": 12, "x2": 10, "y2": 12},
        )
        await s.request(
            "tool",
            {
                "action": "apply",
                "name": "zone",
                "zone": "residential",
                "x1": 4,
                "y1": 11,
                "x2": 10,
                "y2": 11,
            },
        )
        await s.request(
            "tool",
            {
                "action": "apply",
                "name": "service",
                "service": "fire",
                "x1": 5,
                "y1": 13,
            },
        )
        await s.request(
            "tool",
            {
                "action": "apply",
                "name": "district",
                "archetype": "general",
                "x1": 4,
                "y1": 10,
                "x2": 10,
                "y2": 14,
            },
        )
        await s.request(
            "tool", {"action": "district_policy", "id": 1, "budget_cap": 1000}
        )
        await s.request("time", {"action": "step", "ticks": 3})
        await assert_state(s, "state.tick", "eq", 3)
        await assert_state(s, "state.road_tiles", "ge", 7)
        await s.city("save")
        await s.city("load")
        for projection in ("top_down", "isometric"):
            await s.request("view", {"action": "projection", "projection": projection})
            for overlay in range(8):
                await s.request("view", {"action": "overlay", "overlay": overlay})
                result = await s.screenshot(f"{projection}-{overlay}")
                with Image.open(result["png"]) as image:
                    self.assertEqual(image.format, "PNG")
        await s.request("window", {"action": "resize", "width": 1100, "height": 720})
        result = await s.screenshot("resized")
        with Image.open(result["png"]) as image:
            self.assertEqual(
                image.size,
                (
                    result["state"]["dimensions"]["framebuffer_width"],
                    result["state"]["dimensions"]["framebuffer_height"],
                ),
            )
        self.assertEqual(s.last_state["validation_errors"], 0)
        if os.environ.get("VK_LOADER_DEBUG"):
            self.assertIn(
                "VK_LAYER_KHRONOS_validation",
                s.artifact("game.log").read_text(errors="replace"),
            )
        scenario = await record(s, "stop", "acceptance")
        result = await replay(s, scenario["scenario"], 2)
        self.assertTrue(result["identical"])

    async def test_input_parity_coordinates_and_focus_loss(self):
        s = self.session
        await s.city("new", 64)
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request(
            "view", {"action": "toggle", "name": "compact_ui", "enabled": True}
        )
        for projection in ("top_down", "isometric"):
            await s.request("view", {"action": "projection", "projection": projection})
            tx, ty = (8, 12) if projection == "top_down" else (32, 32)
            screen = await s.request(
                "coordinates", {"direction": "tile_to_screen", "x": tx, "y": ty}
            )
            tile = await s.request(
                "coordinates",
                {"direction": "screen_to_tile", "x": screen["x"], "y": screen["y"]},
            )
            self.assertEqual((tile["x"], tile["y"]), (tx, ty))
            # Zoom can produce odd isometric tile heights. Picking must still
            # agree with the rendered tile center and keep the cursor anchor.
            for _ in range(2):
                await s.request(
                    "input",
                    {
                        "events": [
                            {
                                "type": "scroll",
                                "x": screen["x"],
                                "y": screen["y"],
                                "delta": 1,
                            }
                        ]
                    },
                )
                tile = await s.request(
                    "coordinates",
                    {
                        "direction": "screen_to_tile",
                        "x": screen["x"],
                        "y": screen["y"],
                    },
                )
                self.assertEqual((tile["x"], tile["y"]), (tx, ty))
        await s.request(
            "view",
            {"action": "camera", "tile_pixels": 28, "view_x": 0, "view_y": 0},
        )
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request(
            "input",
            {
                "events": [
                    {"type": "key", "key": "R"},
                    {"type": "down", "x": 126, "y": 350},
                    {"type": "move", "x": 294, "y": 350},
                    {"type": "up", "x": 294, "y": 350},
                ]
            },
        )
        first = s.last_state.copy()
        self.assertEqual(first["road_tiles"], 7)
        await s.request(
            "input",
            {
                "events": [
                    {"type": "down", "x": 126, "y": 406},
                    {"type": "move", "x": 294, "y": 406},
                    {"type": "focus_lost"},
                    {"type": "up", "x": 294, "y": 406},
                ]
            },
        )
        self.assertEqual(s.last_state["funds"], first["funds"])
        await s.request(
            "tool",
            {"action": "apply", "name": "road", "x1": 4, "y1": 14, "x2": 10, "y2": 14},
        )
        self.assertEqual(first["funds"] - s.last_state["funds"], 50000 - first["funds"])
        # Invalid placement must preserve the city and leave the session usable.
        before = s.last_state["funds"]
        with self.assertRaises(PlaytestError):
            await s.request(
                "tool", {"action": "apply", "name": "road", "x1": 500, "y1": 0}
            )
        self.assertEqual((await s.request("status"))["state"]["funds"], before)
        for _ in range(4):
            await s.request(
                "window", {"action": "resize", "width": 1100, "height": 720}
            )
        if s.capabilities.get("window_minimize_restore", True):
            await s.request("window", {"action": "minimize"})
            await asyncio.sleep(0.2)
            self.assertIn(
                (await s.request("status"))["state"]["phase"],
                ("gameplay", "quit_dialog"),
            )
            with self.assertRaises(PlaytestError):
                await s.screenshot("minimized")
            await s.request("window", {"action": "close"})
            self.assertEqual(s.last_state["phase"], "quit_dialog")
            await s.request("window", {"action": "restore"})
        else:
            with self.assertRaises(PlaytestError):
                await s.request("window", {"action": "minimize"})
            await s.request("window", {"action": "close"})
            self.assertEqual(s.last_state["phase"], "quit_dialog")
        await s.request("input", {"events": [{"type": "key", "key": "Escape"}]})
        self.assertEqual(s.last_state["phase"], "gameplay")

    async def test_insufficient_funds_import_and_crash_recovery(self):
        s = self.session
        await s.city("new", 64)
        for y in range(7):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "road",
                    "x1": 0,
                    "y1": y,
                    "x2": 63,
                    "y2": y,
                },
            )
        before = s.last_state.copy()
        with self.assertRaises(PlaytestError):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "road",
                    "x1": 0,
                    "y1": 8,
                    "x2": 63,
                    "y2": 8,
                },
            )
        current = (await s.request("status"))["state"]
        self.assertEqual(current["funds"], before["funds"])
        self.assertEqual(current["road_tiles"], before["road_tiles"])
        await s.city("save")
        source = str(s.artifact("urban_sim_session.json"))
        await s.stop()
        await s.launch(terrain_seed=43, simulation_seed=100001)
        self.assertEqual(s.last_state["simulation_seed"], 100001)
        await record(s, "start")
        await s.city("load", import_path=source)
        self.assertEqual(s.last_state["funds"], before["funds"])
        scenario = await record(s, "stop", "imported")
        await replay(s, scenario["scenario"], 2)
        s.process.kill()
        await s.process.wait()
        with self.assertRaises(PlaytestError):
            await s.request("status")
        await s.launch()
        self.assertEqual(s.last_state["phase"], "start_screen")
        with self.assertRaises(PlaytestError):
            await s.city("load")
        self.assertTrue(s.broken)
        await s.launch()

    async def test_sdk_client_and_disconnect_cleanup(self):
        # Stop the fixture game so this test owns only the game launched through MCP.
        await self.session.stop()
        parameters = StdioServerParameters(
            command=sys.executable,
            env=os.environ.copy(),
            args=[
                "-m",
                "urban_sim_mcp.server",
                "--executable",
                EXECUTABLE,
                "--artifacts",
                str(Path(ARTIFACTS).resolve()),
            ],
        )
        pid = None
        async with stdio_client(parameters) as (read, write):
            async with ClientSession(read, write) as client:
                await client.initialize()
                self.assertEqual(len((await client.list_tools()).tools), 16)
                self.assertEqual(len((await client.list_prompts()).prompts), 2)
                self.assertEqual(len((await client.list_resources()).resources), 4)
                launch = await client.call_tool("launch", {})
                self.assertFalse(launch.isError)
                data = launch.structuredContent
                pid = data["pid"]
                await client.call_tool("city", {"action": "new", "size": 32})
                screenshot = await client.call_tool("screenshot", {"name": "sdk"})
                self.assertFalse(screenshot.isError)
                image = next(
                    block for block in screenshot.content if block.type == "image"
                )
                with Image.open(io.BytesIO(base64.b64decode(image.data))) as png:
                    self.assertEqual(png.format, "PNG")
                bad = await client.call_tool("time", {"action": "step", "ticks": 0})
                self.assertTrue(bad.isError)
                resource = await client.read_resource("urban-sim://state")
                self.assertTrue(resource.contents)
                prompt = await client.get_prompt(
                    "regression_investigation", {"symptom": "test"}
                )
                self.assertTrue(prompt.messages)
        # psutil is a transitive SDK dependency only on some platforms; use native process checks.
        await asyncio.sleep(0.5)
        if os.name == "nt":
            import ctypes

            handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
            if handle:
                code = ctypes.c_ulong()
                ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
                ctypes.windll.kernel32.CloseHandle(handle)
                self.assertNotEqual(
                    code.value, 259, "Owned game survived MCP disconnect"
                )
        else:
            with self.assertRaises(ProcessLookupError):
                os.kill(pid, 0)


if __name__ == "__main__":
    unittest.main()
