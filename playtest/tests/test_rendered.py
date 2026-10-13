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

    async def test_focus_centers_developed_area_in_both_projections(self):
        s = self.session
        await s.city("new", 64)
        await s.request("tool", {"action": "apply", "name": "road",
                                "x1": 40, "y1": 40, "x2": 44, "y2": 40})
        for projection in ("top_down", "isometric"):
            await s.request("view", {"action": "projection", "projection": projection})
            await s.request("view", {"action": "focus"})
            point = await s.request("coordinates", {"direction": "tile_to_screen",
                                                    "x": 42, "y": 40})
            dimensions = s.last_state["dimensions"]
            self.assertLessEqual(abs(point["x"] - dimensions["width"] / 2), 28)
            self.assertLessEqual(abs(point["y"] - dimensions["height"] / 2), 28)
            await s.screenshot("focused-" + projection)

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

    async def test_playable_migration_unlocks_transit_and_survives_load(self):
        s = self.session
        await s.city("new", 64)
        await s.request(
            "tool",
            {"action": "apply", "name": "road", "x1": 8, "y1": 32, "x2": 40, "y2": 32},
        )
        for service, x, y in (
            ("power", 24, 31),
            ("water", 24, 33),
            ("education", 25, 31),
        ):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "service",
                    "service": service,
                    "x1": x,
                    "y1": y,
                },
            )
        for zone, x1, x2, y in (
            ("residential", 8, 22, 31),
            ("commercial", 8, 22, 33),
            ("industrial", 28, 40, 31),
            ("office", 28, 40, 33),
        ):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "zone",
                    "zone": zone,
                    "x1": x1,
                    "y1": y,
                    "x2": x2,
                    "y2": y,
                },
            )
        for _ in range(10):
            await s.request("time", {"action": "step", "ticks": 30})
            if s.last_state["funds"] >= 250000:
                break
        self.assertGreaterEqual(s.last_state["funds"], 250000)
        for service, x, y in (
            ("health", 26, 31),
            ("sanitation", 26, 33),
            ("police", 23, 31),
            ("fire", 23, 33),
            ("garbage", 27, 31),
            ("cemetery", 27, 33),
        ):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "service",
                    "service": service,
                    "x1": x,
                    "y1": y,
                },
            )
        # Expand into road-connected, utility-served neighborhoods rather than
        # relying on redevelopment along a saturated single-road village.
        await s.request(
            "tool",
            {"action": "apply", "name": "road", "x1": 40, "y1": 32, "x2": 42, "y2": 36},
        )
        for y in range(36, 61, 4):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "road",
                    "x1": 10,
                    "y1": y,
                    "x2": 42,
                    "y2": y,
                },
            )
        for x in range(10, 43, 4):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "road",
                    "x1": x,
                    "y1": 36,
                    "x2": x,
                    "y2": 60,
                },
            )
        for x in (15, 35):
            for service, sx, y in (
                ("power", x, 47),
                ("water", x, 49),
                ("education", x + 2, 47),
                ("health", x + 2, 49),
                ("garbage", x, 51),
            ):
                await s.request(
                    "tool",
                    {
                        "action": "apply",
                        "name": "service",
                        "service": service,
                        "x1": sx,
                        "y1": y,
                    },
                )
        for x, y in ((20, 43), (32, 51)):
            for service, sx, sy in (
                ("sanitation", x, y),
                ("cemetery", x, y + 2),
                ("fire", x + 1, y),
                ("police", x + 1, y + 2),
            ):
                await s.request(
                    "tool",
                    {
                        "action": "apply",
                        "name": "service",
                        "service": service,
                        "x1": sx,
                        "y1": sy,
                    },
                )
        reserved = {
            (14, 44),
            (14, 48),
            (34, 44),
            (34, 48),
            (18, 40),
            (18, 44),
            (30, 48),
            (30, 52),
        }
        for ix, x in enumerate(range(10, 42, 4)):
            for iy, y in enumerate(range(36, 60, 4)):
                if (x, y) in reserved:
                    continue
                zone = (
                    "residential"
                    if (ix + iy) % 3 != 2
                    else ("commercial" if iy % 2 == 0 else "industrial")
                )
                await s.request(
                    "tool",
                    {
                        "action": "apply",
                        "name": "zone",
                        "zone": zone,
                        "x1": x + 1,
                        "y1": y + 1,
                        "x2": x + 3,
                        "y2": y + 3,
                    },
                )
        await s.request(
            "tool",
            {
                "action": "apply",
                "name": "district",
                "archetype": "general",
                "x1": 10,
                "y1": 36,
                "x2": 42,
                "y2": 60,
            },
        )
        for _ in range(12):
            await s.request("time", {"action": "step", "ticks": 50})
            flow = s.last_state["population_change"]
            self.assertEqual(
                flow["net"], flow["arrivals"] - flow["departures"] - flow["deaths"]
            )
            self.assertGreater(s.last_state["population"], 0)
            if s.last_state["population"] > 1200 and s.last_state["routes"] > 0:
                break
        self.assertGreater(s.last_state["population"], 1200)
        self.assertGreater(s.last_state["routes"], 0)
        entities = await s.request("inspect", {"section": "entities", "limit": 512})
        homes = [b for b in entities["items"] if b["type"] == 1 and b["occupancy"] > 0]
        self.assertGreater(len(homes), 1)
        population = sum(b["occupancy"] for b in homes)
        self.assertEqual(population, s.last_state["population"])
        for field in ("crime_rate", "illness_rate"):
            weighted = sum(b[field] * b["occupancy"] for b in homes) / population
            self.assertAlmostEqual(weighted, s.last_state["metrics"][field], places=6)
        self.assertGreater(
            max(b["crime_rate"] for b in homes), min(b["crime_rate"] for b in homes)
        )
        self.assertGreater(
            max(b["illness_rate"] for b in homes), min(b["illness_rate"] for b in homes)
        )
        first = homes[0]
        tiles = await s.request(
            "inspect", {"section": "tiles", "x": first["x"], "y": first["y"]}
        )
        self.assertEqual(tiles["items"][0]["crime_rate"], first["crime_rate"])
        self.assertEqual(tiles["items"][0]["illness_rate"], first["illness_rate"])
        await s.city("save")
        await s.request("time", {"action": "step", "ticks": 10})
        expected = s.last_state.copy()
        await s.city("load")
        await s.request("time", {"action": "step", "ticks": 10})
        for field in (
            "population",
            "funds",
            "tick",
            "buildings",
            "routes",
            "population_change",
        ):
            self.assertEqual(s.last_state[field], expected[field], field)
        await s.request(
            "view", {"action": "toggle", "name": "compact_ui", "enabled": False}
        )
        await s.screenshot("migration-population-hud")
        await s.request(
            "view", {"action": "toggle", "name": "compact_ui", "enabled": True}
        )
        await s.screenshot("migration-compact-hud")
        await s.request(
            "view", {"action": "toggle", "name": "compact_ui", "enabled": False}
        )
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request("view", {"action": "overlay", "overlay": 6})
        await s.request(
            "view",
            {
                "action": "camera",
                "tile_pixels": 28,
                "view_x": max(0, first["x"] - 20),
                "view_y": max(0, first["y"] - 12),
            },
        )
        position = await s.request(
            "coordinates",
            {"direction": "tile_to_screen", "x": first["x"], "y": first["y"]},
        )
        await s.request(
            "input",
            {"events": [{"type": "move", "x": position["x"], "y": position["y"]}]},
        )
        await s.screenshot("neighborhood-happiness-inspector")

    async def test_freight_gate_arterial_upgrade_and_checkpoint(self):
        s = self.session
        await s.city("new", 32)
        await s.request(
            "tool",
            {"action": "apply", "name": "road", "x1": 0, "y1": 16, "x2": 24, "y2": 16},
        )
        for service, y in (("power", 15), ("water", 17)):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "service",
                    "service": service,
                    "x1": 12,
                    "y1": y,
                },
            )
        for zone, x1, x2, y in (
            ("residential", 2, 8, 15),
            ("commercial", 16, 22, 17),
            ("industrial", 16, 22, 15),
        ):
            await s.request(
                "tool",
                {
                    "action": "apply",
                    "name": "zone",
                    "zone": zone,
                    "x1": x1,
                    "y1": y,
                    "x2": x2,
                    "y2": y,
                },
            )
        for _ in range(6):
            await s.request("time", {"action": "step", "ticks": 40})
            if s.last_state["metrics"]["freight_trucks"] > 0:
                break
        self.assertGreater(s.last_state["metrics"]["freight_trucks"], 0)
        self.assertTrue(s.last_state["metrics"]["freight_gate"]["connected"])

        def gate_edge(result):
            return next(e for e in result["items"] if e["x1"] == 0 and e["y1"] == 16)

        before = gate_edge(await s.request("inspect", {"section": "roads"}))
        self.assertGreater(before["freight_load"], 0)
        self.assertLess(before["load"], 40)
        cash = s.last_state["funds"]
        await s.request(
            "tool", {"action": "select", "name": "road", "road_class": "local"}
        )
        await s.request("input", {"events": [{"type": "key", "key": "A"}]})
        self.assertEqual(s.last_state["tools"]["road_class"], "arterial")
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request(
            "view", {"action": "camera", "tile_pixels": 40, "view_x": 0, "view_y": 10}
        )
        start = await s.request(
            "coordinates", {"direction": "tile_to_screen", "x": 0, "y": 16}
        )
        end = await s.request(
            "coordinates", {"direction": "tile_to_screen", "x": 2, "y": 16}
        )
        await s.request(
            "input",
            {
                "events": [
                    {"type": "down", "x": start["x"], "y": start["y"]},
                    {"type": "move", "x": end["x"], "y": end["y"]},
                    {"type": "up", "x": end["x"], "y": end["y"]},
                ]
            },
        )
        after = gate_edge(await s.request("inspect", {"section": "roads"}))
        self.assertEqual(s.last_state["funds"], cash - 600)
        self.assertEqual(after["road_class"], "arterial")
        self.assertEqual(after["capacity"], 40)
        self.assertEqual(after["freight_load"], before["freight_load"])
        self.assertLess(after["congestion"], before["congestion"])
        await s.city("save")
        await s.request("time", {"action": "step", "ticks": 5})
        expected = s.last_state.copy()
        await s.city("load")
        loaded = gate_edge(await s.request("inspect", {"section": "roads"}))
        self.assertEqual(loaded["road_class"], "arterial")
        await s.request("time", {"action": "step", "ticks": 5})
        for field in ("population", "funds", "tick", "population_change"):
            self.assertEqual(s.last_state[field], expected[field], field)
        for field in (
            "freight_trucks",
            "freight_domestic_goods",
            "freight_exported_goods",
            "freight_imported_goods",
            "freight_unrouted_goods",
        ):
            self.assertEqual(
                s.last_state["metrics"][field], expected["metrics"][field], field
            )
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request(
            "view", {"action": "camera", "tile_pixels": 40, "view_x": 0, "view_y": 10}
        )
        await s.request("view", {"action": "overlay", "overlay": 4})
        await s.screenshot("freight-arterial-congestion")

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
                self.assertEqual(len((await client.list_resources()).resources), 5)
                launch = await client.call_tool("launch", {})
                self.assertFalse(launch.isError)
                data = launch.structuredContent
                pid = data["pid"]
                self.assertEqual(data["capabilities"]["inspection_version"], 3)
                self.assertTrue(data["capabilities"]["features"]["neighborhood_rates"])
                self.assertTrue(data["capabilities"]["features"]["freight"])
                self.assertTrue(data["capabilities"]["features"]["road_classes"])
                status = await client.call_tool("status", {})
                self.assertEqual(status.structuredContent["server_version"], "0.3.0")
                reference = await client.read_resource("urban-sim://inspection")
                self.assertEqual(
                    json.loads(reference.contents[0].text)["inspection_version"], 3
                )

                await client.call_tool("city", {"action": "new", "size": 32})
                summary = await client.call_tool("inspect", {"section": "summary"})
                self.assertFalse(summary.isError)
                self.assertEqual(
                    summary.structuredContent["state"]["population_change"]["net"], 0
                )
                self.assertIn(
                    "sanitation_coverage", summary.structuredContent["state"]["metrics"]
                )
                self.assertIn(
                    "deathcare_backlog", summary.structuredContent["state"]["metrics"]
                )
                road = await client.call_tool(
                    "tool",
                    {
                        "action": "apply",
                        "name": "road",
                        "x1": 6,
                        "y1": 12,
                        "x2": 10,
                        "y2": 12,
                    },
                )
                self.assertFalse(road.isError)
                for service, y in (("power", 11), ("water", 13)):
                    plant = await client.call_tool(
                        "tool",
                        {
                            "action": "apply",
                            "name": "service",
                            "service": service,
                            "x1": 7,
                            "y1": y,
                        },
                    )
                    self.assertFalse(plant.isError)
                plants = await client.call_tool("inspect", {"section": "facilities"})
                self.assertFalse(plants.isError)
                by_type = {
                    plant["type"].lower(): plant
                    for plant in plants.structuredContent["items"]
                }
                self.assertGreater(by_type["power"]["power_capacity_mw"], 0)
                self.assertIn("power_source", by_type["power"])
                self.assertIn("emissions_kg_per_mwh", by_type["power"])
                self.assertGreater(by_type["water"]["water_supply"], 0)
                for zone, y in (("residential", 11), ("commercial", 13)):
                    zoned = await client.call_tool(
                        "tool",
                        {
                            "action": "apply",
                            "name": "zone",
                            "zone": zone,
                            "x1": 8,
                            "y1": y,
                            "x2": 10,
                            "y2": y,
                        },
                    )
                    self.assertFalse(zoned.isError)
                stepped = await client.call_tool(
                    "time", {"action": "step", "ticks": 40}
                )
                self.assertFalse(stepped.isError)
                entities = await client.call_tool("inspect", {"section": "entities"})
                homes = [
                    b for b in entities.structuredContent["items"] if b["type"] == 1
                ]
                self.assertTrue(homes)
                for home in homes:
                    self.assertGreaterEqual(home["crime_rate"], 0)
                    self.assertLessEqual(home["illness_rate"], 1)
                tile = await client.call_tool(
                    "inspect",
                    {"section": "tiles", "x": homes[0]["x"], "y": homes[0]["y"]},
                )
                self.assertEqual(
                    tile.structuredContent["items"][0]["crime_rate"],
                    homes[0]["crime_rate"],
                )
                self.assertEqual(
                    tile.structuredContent["items"][0]["illness_rate"],
                    homes[0]["illness_rate"],
                )
                upgraded = await client.call_tool(
                    "tool",
                    {
                        "action": "apply",
                        "name": "road",
                        "road_class": "arterial",
                        "x1": 6,
                        "y1": 12,
                        "x2": 10,
                        "y2": 12,
                    },
                )
                self.assertFalse(upgraded.isError)
                edges = await client.call_tool("inspect", {"section": "roads"})
                self.assertFalse(edges.isError)
                self.assertTrue(edges.structuredContent["items"])
                self.assertTrue(
                    all(
                        e["road_class"] == "arterial" and e["capacity"] == 40
                        for e in edges.structuredContent["items"]
                    )
                )
                wrong_section = await client.call_tool("inspect", {"section": "state"})
                self.assertTrue(wrong_section.isError)
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
