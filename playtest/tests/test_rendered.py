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

    async def test_shore_treatment_replacement_funding_and_checkpoint(self):
        s = self.session
        await s.city("new", 32)
        for x in (10, 16):
            await s.request("tool", {"action":"apply", "name":"road", "x1":x, "y1":5, "x2":x, "y2":12})
        await s.city("save")
        core_path = s.artifact("urban_sim_session.json.city.json")
        core = json.loads(core_path.read_text())
        for tile in core["tiles"]:
            if tile["x"] in (12, 14):
                tile["type"] = 2
        core_path.write_text(json.dumps(core))
        await s.city("load")
        for service, x, y, shore in (("water",11,8,True), ("water",15,8,True),
                                      ("sanitation",11,10,True), ("power",9,6,False)):
            await s.request("tool", {"action":"apply", "name":"service", "service":service,
                                     "x1":x, "y1":y, "surface_water":shore})
        await s.request("tool", {"action":"apply", "name":"zone", "zone":"residential", "x1":9, "y1":8})
        await s.request("time", {"action":"step", "ticks":20})
        facilities = (await s.request("inspect", {"section":"facilities"}))["items"]
        water = [f for f in facilities if f["type"] == "Water"]
        self.assertEqual(len(water), 2)
        self.assertLess(water[0]["source_clean_fraction"], 1.)
        self.assertEqual(water[1]["source_clean_fraction"], 1.)
        self.assertGreater(s.last_state["budget"]["service_coverage"][6], 0.)
        # Select raw sanitation and cycle V to treatment, then buy at the raw site.
        await s.request("tool", {"action":"select", "name":"service", "service":"sanitation", "surface_water":True})
        await s.request("input", {"events":[{"type":"key", "key":"V"}]})
        self.assertTrue(s.last_state["tools"]["sewage_treatment"])
        await s.request("window", {"action":"resize", "width":800, "height":640})
        await s.request("view", {"action":"projection", "projection":"top_down"})
        await s.request("view", {"action":"camera", "tile_pixels":40, "view_x":3, "view_y":2})
        point = await s.request("coordinates", {"direction":"tile_to_screen", "x":11, "y":10})
        await s.request("input", {"events":[{"type":"move", "x":point["x"], "y":point["y"]}]})
        self.assertEqual(s.last_state["preview"]["service"]["cost"], 9000)
        self.assertTrue(s.last_state["preview"]["service"]["valid"])
        await s.screenshot("shore-treatment-preview")
        funds = s.last_state["funds"]
        await s.request("input", {"events":[{"type":"down", "button":"left", "x":point["x"], "y":point["y"]},
                                                           {"type":"up", "button":"left", "x":point["x"], "y":point["y"]}]})
        self.assertEqual(s.last_state["funds"], funds - 9000)
        await s.request("time", {"action":"step", "ticks":1})
        facilities = (await s.request("inspect", {"section":"facilities"}))["items"]
        self.assertEqual(len(facilities), 4)
        treatment = next(f for f in facilities if f["type"] == "Sanitation")
        self.assertTrue(treatment["sewage_treatment"])
        self.assertEqual(treatment["name"], "Shore Treatment")
        self.assertTrue(all(f["source_clean_fraction"] == 1. for f in facilities if f["type"] == "Water"))
        self.assertEqual(s.last_state["budget"]["service_costs"][6], 34)
        await s.request("input", {"events":[{"type":"key", "key":"Escape"}]})
        await s.screenshot("shore-treatment-inspector")
        await s.request("budget", {"action":"services"})
        await s.screenshot("shore-treatment-sanitation-panel")
        await s.request("budget", {"action":"close"})
        await s.city("save")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core_copy = s.artifact("treatment-checkpoint.city.json")
        core_copy.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core_copy)
        checkpoint = s.artifact("treatment-checkpoint.json")
        checkpoint.write_text(json.dumps(frozen))
        await s.request("time", {"action":"step", "ticks":3})
        await s.city("save")
        expected = json.loads(s.artifact("urban_sim_session.json").read_text())
        expected_core = json.loads(Path(expected["citySnapshot"]).read_text())
        await s.city("load", import_path=str(checkpoint))
        await s.request("time", {"action":"step", "ticks":3})
        await s.city("save")
        restored = json.loads(s.artifact("urban_sim_session.json").read_text())
        self.assertEqual(json.loads(Path(restored["citySnapshot"]).read_text()), expected_core)
        self.assertEqual({k:v for k,v in restored.items() if k != "citySnapshot"},
                         {k:v for k,v in expected.items() if k != "citySnapshot"})
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest", "-cli"))
        output = s.artifact("treatment-headless.json")
        process = await asyncio.create_subprocess_exec(str(cli), "--simulate", "3",
            "--simulate-session", str(checkpoint), "--simulate-session-out", str(output),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        _, stderr = await process.communicate()
        self.assertEqual(process.returncode, 0, stderr.decode())
        headless = json.loads(output.read_text())
        self.assertEqual(json.loads(Path(headless["citySnapshot"]).read_text()), expected_core)
        self.assertEqual({k:v for k,v in headless.items() if k != "citySnapshot"},
                         {k:v for k,v in expected.items() if k != "citySnapshot"})
        funding = [100] * 12
        funding[6] = 0
        await s.request("budget", {"action":"set_funding", "service_percent":funding})
        await s.request("time", {"action":"step", "ticks":1})
        self.assertEqual(s.last_state["budget"]["service_costs"][6], 0)
        self.assertEqual(s.last_state["budget"]["service_coverage"][6], 0.)
        facilities = (await s.request("inspect", {"section":"facilities"}))["items"]
        self.assertTrue(all(f["source_clean_fraction"] == 1. for f in facilities if f["type"] == "Water"))
        with self.assertRaises(PlaytestError):
            await s.request("tool", {"action":"apply", "name":"service", "service":"water",
                                     "surface_water":True, "sewage_treatment":True, "x1":11, "y1":9})

    async def test_local_fire_response_distance_and_checkpoint(self):
        s = self.session
        await s.city("new", 32)
        await s.request("tool", {"action":"apply", "name":"road", "x1":6, "y1":16, "x2":26, "y2":16})
        await s.request("tool", {"action":"apply", "name":"service", "service":"fire", "x1":8, "y1":15})
        await s.request("time", {"action":"step", "ticks":1})
        await s.city("save")
        fixture = json.loads(s.artifact("urban_sim_session.json").read_text())
        station = next(f for f in fixture["facilities"] if f["type"] == 0)
        station["maxTravelDistance"] = 4
        s.artifact("urban_sim_session.json").write_text(json.dumps(fixture))
        await s.city("load")
        values = []
        for x in (8, 10, 12, 20):
            d = await s.request("inspect", {"section":"diagnostics", "x":x, "y":17})
            values.append(d["local_fire_response"])
        for actual, expected in zip(values, (1., .6, .2, 0.)):
            self.assertAlmostEqual(actual, expected, places=5)
        await s.city("save")
        await s.city("load")
        restored = await s.request("inspect", {"section":"diagnostics", "x":10, "y":17})
        self.assertAlmostEqual(restored["local_fire_response"], values[1], places=5)
        await s.request("window", {"action":"resize", "width":800, "height":640})
        await s.request("view", {"action":"projection", "projection":"top_down"})
        await s.request("view", {"action":"camera", "tile_pixels":40, "view_x":3, "view_y":7})
        point = await s.request("coordinates", {"direction":"tile_to_screen", "x":10, "y":17})
        await s.request("input", {"events":[{"type":"move", "x":point["x"], "y":point["y"]}]})
        await s.screenshot("local-fire-response-inspector")

    async def test_school_seats_hospital_beds_funding_and_checkpoint(self):
        s = self.session
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 18), ("water", 20)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        await s.city("save")
        fixture = json.loads(s.artifact("urban_sim_session.json").read_text())
        core = json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        home = next(b for b in core["buildings"] if b["type"] == 1)
        job = next(b for b in core["buildings"] if b["type"] == 2)
        for b in core["buildings"]:
            b["occupancy"] = 0
        home["capacity"] = job["capacity"] = 10000
        home["occupancy"] = job["occupancy"] = 5000
        for group, size in zip(core["populationGroups"], (2500, 1750, 750)):
            group["size"] = group["employed"] = size
        core_path = s.artifact("civic-capacity.city.json")
        core_path.write_text(json.dumps(core))
        fixture["citySnapshot"] = str(core_path)
        fixture["funds"] = 1000000
        for kind, x in ((3, 14), (2, 16)):
            fixture["facilities"].append({"type": kind, "x": x, "y": 15, "maxTravelDistance": 30, "quality": 1})
        path = s.artifact("civic-capacity.json")
        path.write_text(json.dumps(fixture))
        await s.city("load", import_path=str(path))
        await s.request("time", {"action": "step", "ticks": 1})
        metrics = s.last_state["metrics"]
        self.assertEqual(metrics["school_seats"], 800)
        self.assertEqual(metrics["hospital_beds"], 400)
        self.assertLess(metrics["effective_education_coverage"], metrics["education_coverage"])
        self.assertLess(metrics["hospital_capacity_factor"], 1)
        self.assertAlmostEqual(metrics["patient_demand"], s.last_state["population"] * metrics["illness_rate"], delta=.01)
        await s.screenshot("civic-capacity-hud")
        funding = [100] * 12
        funding[2] = funding[3] = 50
        await s.request("budget", {"action": "set_funding", "service_percent": funding})
        self.assertEqual(s.last_state["metrics"]["school_seats"], 400)
        self.assertEqual(s.last_state["metrics"]["hospital_beds"], 200)
        await s.request("budget", {"action": "reset_funding"})
        for service, x, y in (("education",14,17),("education",16,17),
            ("health",15,15),("health",17,15),("health",19,15),("health",21,15),("health",22,15),
            ("health",15,17),("health",17,17)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": y})
        await s.request("time", {"action": "step", "ticks": 2})
        metrics = s.last_state["metrics"]
        self.assertEqual(metrics["effective_education_coverage"], metrics["education_coverage"])
        self.assertEqual(metrics["hospital_capacity_factor"], 1)
        await s.city("save")
        await s.request("time", {"action": "step", "ticks": 2})
        expected = s.last_state.copy()
        await s.city("load")
        await s.request("time", {"action": "step", "ticks": 2})
        for key in ("population", "funds", "tick"):
            self.assertEqual(s.last_state[key], expected[key])
        for key in ("effective_education_coverage", "hospital_capacity_factor", "illness_rate"):
            self.assertEqual(s.last_state["metrics"][key], expected["metrics"][key])
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request("view", {"action": "camera", "tile_pixels": 40, "view_x": 5, "view_y": 5})
        point = await s.request("coordinates", {"direction": "tile_to_screen", "x": 14, "y": 15})
        await s.request("input", {"events": [{"type": "move", "x": point["x"], "y": point["y"]}]})
        await s.screenshot("civic-capacity-inspector")
        await s.request("view", {"action": "toggle", "name": "compact_ui", "enabled": True})
        await s.screenshot("civic-capacity-compact-hud")

    async def test_landfill_full_alert_storage_and_incinerator_recovery(self):
        s = self.session
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 18), ("water", 20)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        await s.city("save")
        fixture = json.loads(s.artifact("urban_sim_session.json").read_text())
        fixture["citySnapshot"] = str(s.artifact("urban_sim_session.json.city.json"))
        fixture["funds"] = 100000
        fixture["facilities"].append({"type": 7, "x": 16, "y": 15,
            "maxTravelDistance": 30, "quality": 1, "landfillRemaining": 1})
        path = s.artifact("landfill-fixture.json")
        path.write_text(json.dumps(fixture))
        await s.city("load", import_path=str(path))
        sites = (await s.request("inspect", {"section": "facilities"}))["items"]
        self.assertEqual(next(f for f in sites if f["type"] == "Garbage")["landfill_remaining"], 1)
        await s.request("time", {"action": "step", "ticks": 3})
        self.assertEqual(s.last_state["metrics"]["landfill_remaining"], 0)
        await s.city("save")
        checkpoint = json.loads(s.artifact("urban_sim_session.json").read_text())
        self.assertTrue(checkpoint["cityAlerts"]["records"][5]["active"])
        await s.city("load")
        sites = (await s.request("inspect", {"section": "facilities"}))["items"]
        self.assertEqual(next(f for f in sites if f["type"] == "Garbage")["landfill_remaining"], 0)
        await s.request("input", {"events": [{"type": "key", "key": "F7"}]})
        await s.screenshot("landfill-full-alert")
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.request("budget", {"action": "services"})
        await s.screenshot("landfill-budget-volume")
        await s.request("budget", {"action": "close"})
        await s.request("view", {"action": "projection", "projection": "top_down"})
        await s.request("view", {"action": "camera", "tile_pixels": 40, "view_x": 7, "view_y": 5})
        point = await s.request("coordinates", {"direction": "tile_to_screen", "x": 16, "y": 15})
        await s.request("input", {"events": [{"type": "move", "x": point["x"], "y": point["y"]}]})
        await s.screenshot("landfill-inspector")
        await s.request("tool", {"action": "apply", "name": "service", "service": "incinerator", "x1": 22, "y1": 15})
        await s.request("time", {"action": "step", "ticks": 2})
        self.assertGreater(s.last_state["metrics"]["landfill_remaining"], 0)
        self.assertGreater(s.last_state["metrics"]["waste_stored_incinerated"], 0)
        self.assertGreater(s.last_state["metrics"]["waste_power_mw"], 0)
        await s.city("save")
        recovered = json.loads(s.artifact("urban_sim_session.json").read_text())
        self.assertFalse(recovered["cityAlerts"]["records"][5]["active"])

    async def test_incinerator_native_purchase_fuel_funding_and_checkpoint(self):
        s = self.session
        await record(s, "start")
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 18), ("water", 20)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        # Generate treasury through ordinary simulation, then buy through native input.
        for _ in range(20):
            if s.last_state["funds"] >= 18000:
                break
            await s.request("time", {"action": "step", "ticks": 50})
        self.assertGreaterEqual(s.last_state["funds"], 18000)
        await s.request("tool", {"action": "select", "name": "service", "service": "crematorium"})
        await s.request("input", {"events": [{"type": "key", "key": "S"}]})
        await s.request("view", {"action": "projection", "projection": "top_down"})
        point = await s.request("coordinates", {"direction": "tile_to_screen", "x": 22, "y": 15})
        before = s.last_state["funds"]
        await s.request("input", {"events": [{"type": "down", "x": point["x"], "y": point["y"]},
                                               {"type": "up", "x": point["x"], "y": point["y"]}]})
        self.assertEqual(s.last_state["funds"], before - 18000)
        plants = (await s.request("inspect", {"section": "facilities"}))["items"]
        plant = next(f for f in plants if f["type"] == "Incinerator")
        self.assertGreater(plant["city_waste_power_mw"], 0)
        await s.city("save")
        await s.city("load")
        await s.request("time", {"action": "step", "ticks": 2})
        self.assertGreater(s.last_state["metrics"]["waste_power_mw"], 0)
        self.assertGreater(s.last_state["metrics"]["waste_incinerated"], 0)
        self.assertEqual(s.last_state["budget"]["service_costs"][11], 60)
        await s.request("view", {"action": "projection", "projection": "isometric"})
        point = await s.request("coordinates", {"direction": "tile_to_screen", "x": 22, "y": 15})
        await s.request("input", {"events": [{"type": "move", "x": point["x"], "y": point["y"]}]})
        await s.screenshot("incinerator-power-inspector")
        await s.request("budget", {"action": "services"})
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        controls = (await s.request("inspect", {"section": "ui"}))["controls"]
        button = next(c for c in controls if c["name"] == "funding_11_minus")
        for _ in range(4):
            await s.request("input", {"events": [{"type": "down", "x": button["x"]+2, "y": button["y"]+2},
                                                   {"type": "up", "x": button["x"]+2, "y": button["y"]+2}]})
        self.assertEqual(s.last_state["budget"]["service_percent"][11], 0)
        plant = next(f for f in (await s.request("inspect", {"section": "facilities"}))["items"] if f["type"] == "Incinerator")
        self.assertEqual(plant["city_waste_power_mw"], 0)
        await s.screenshot("incinerator-funding-minimum-window")
        await s.city("save")
        await s.city("load")
        self.assertEqual(s.last_state["budget"]["service_percent"][11], 0)
        self.assertEqual(s.last_state["budget"]["service_costs"][11], 0)

        scenario = await record(s, "stop", "incinerator")
        self.assertTrue((await replay(s, scenario["scenario"], 2))["identical"])

    async def test_native_default_river_shore_utilities_and_checkpoint(self):
        s = self.session
        await record(s, "start")
        self.assertTrue((await s.request("inspect", {"section": "ui"}))["terrain"])

        async def keys(*names):
            await s.request(
                "input", {"events": [{"type": "key", "key": k} for k in names]}
            )

        async def click(x, y):
            await s.request(
                "input",
                {
                    "events": [
                        {"type": "move", "x": x, "y": y},
                        {"type": "down", "button": "left", "x": x, "y": y},
                        {"type": "up", "button": "left", "x": x, "y": y},
                    ]
                },
            )

        controls = (await s.request("inspect", {"section": "ui"}))["controls"]
        button = next(c for c in controls if c["name"] == "new")
        await click(button["x"] + 10, button["y"] + 10)
        await keys("F3", "F4", *(["Minus"] * 14))
        tiles = {}
        for y in range(0, 64, 8):
            rows = (
                await s.request(
                    "inspect",
                    {
                        "section": "tiles",
                        "x": 0,
                        "y": y,
                        "width": 64,
                        "height": 8,
                        "limit": 512,
                    },
                )
            )["items"]
            tiles.update({(t["x"], t["y"]): t["type"] for t in rows})
        candidates = [
            (x, y)
            for y in range(8, 29)
            for x in range(2, 61)
            if all(tiles[(x - d, y)] != 2 for d in (0, 1, 2))
            and any(
                tiles[(x + dx, y + dy)] == 2
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))
            )
        ]
        pairs = [
            (a, b)
            for a in candidates
            for b in candidates
            if 2 <= abs(a[1] - b[1]) and abs(a[0] - b[0]) + abs(a[1] - b[1]) <= 4
        ]
        self.assertTrue(pairs)
        intake, outlet = min(
            pairs,
            key=lambda pair: abs(pair[0][0] - pair[1][0])
            + abs(pair[0][1] - pair[1][1]),
        )

        async def screen(pos):
            p = await s.request(
                "coordinates", {"direction": "tile_to_screen", "x": pos[0], "y": pos[1]}
            )
            return {"x": p["x"], "y": p["y"]}

        await keys("R")
        for x, y in (intake, outlet):
            a, b = await screen((x - 2, y)), await screen((x - 1, y))
            await s.request(
                "input",
                {
                    "events": [
                        {"type": "down", "button": "left", **a},
                        {"type": "move", **b},
                        {"type": "up", "button": "left", **b},
                    ]
                },
            )
        await keys("S")
        while s.last_state["tools"]["service_type"] != 5:
            await keys("S")
        self.assertTrue(s.last_state["tools"]["surface_water"])
        await keys("V")
        self.assertFalse(s.last_state["tools"]["surface_water"])
        await keys("V")
        self.assertTrue(s.last_state["tools"]["surface_water"])
        before = s.last_state["funds"]
        await click(**(await screen(intake)))
        self.assertEqual(s.last_state["funds"], before - 3000)
        await keys("S")
        await click(**(await screen(outlet)))
        self.assertEqual(s.last_state["funds"], before - 5000)
        facilities = (await s.request("inspect", {"section": "facilities"}))["items"]
        self.assertEqual(len(facilities), 2)
        self.assertTrue(all(f["surface_water"] for f in facilities))
        self.assertLess(facilities[0]["source_clean_fraction"], 1)
        await keys("F4", "F5")
        await s.screenshot("shore-intake-and-raw-outlet")
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await keys("F3", "F", "F4")
        await s.screenshot("shore-isometric-minimum")
        funds = s.last_state["funds"]
        await keys("N", "F9")
        self.assertEqual(s.last_state["funds"], funds)
        self.assertEqual(
            (await s.request("inspect", {"section": "facilities"}))["items"], facilities
        )
        result = await record(s, "stop", "shore-utilities")
        self.assertTrue((await replay(s, result["scenario"], 2))["identical"])

    async def test_city_guide_recognizes_early_utilities_and_paused_steps(self):
        s = self.session
        await s.city("new", 32)

        async def key(name):
            await s.request("input", {"events": [{"type": "key", "key": name}]})

        if not s.last_state["paused"]:
            await key("Space")
        # Semantic setup isolates the guide regression; the M36 journey uses
        # native construction throughout. Utilities deliberately precede zones.
        await s.request(
            "tool",
            {"action": "apply", "name": "road", "x1": 8, "y1": 16, "x2": 24, "y2": 16},
        )
        for kind, x in (("power", 15), ("water", 17)):
            await s.request("tool", {"action": "apply", "name": "service",
                "service": kind, "x1": x, "y1": 15})
        for kind, x1, x2 in (("residential", 8, 12), ("commercial", 20, 24)):
            await s.request("tool", {"action": "apply", "name": "zone",
                "zone": kind, "x1": x1, "y1": 15, "x2": x2, "y2": 15})
        await key("N")
        self.assertTrue(s.last_state["paused"])
        self.assertEqual(s.last_state["tick"], 1)
        self.assertEqual(s.last_state["toast"], "STEPPED TO TICK 1 - AUTO RUN PAUSED")
        self.assertEqual(s.last_state["ui"]["onboarding_step"], 5)
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.screenshot("m36-guide-budget-minimum")
        await key("F6")
        self.assertEqual(s.last_state["ui"]["onboarding_step"], 6)
        await key("Escape")
        await key("F1")
        self.assertEqual(s.last_state["ui"]["onboarding_step"], 5)

    async def test_regional_management_policy_measurements_and_minimum_layout(self):
        s=self.session
        await s.city("new",32)
        async def key(name):
            await s.request("input",{"events":[{"type":"key","key":name}]})
        async def control(name):
            controls=(await s.request("inspect",{"section":"ui"}))["controls"]
            c=next(c for c in controls if c["name"]==name)
            await s.request("input",{"events":[
                {"type":"down","button":"left","x":c["x"]+4,"y":c["y"]+4},
                {"type":"up","button":"left","x":c["x"]+4,"y":c["y"]+4}]})
        await control("connections")
        self.assertTrue(s.last_state["ui"]["regional"])
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        self.assertFalse(next(c for c in controls if c["name"]=="regional_capacity")["enabled"])
        await key("U")
        for x1,y1,x2,y2 in ((0,10,26,10),(17,13,26,13),(17,10,17,13)):
            await s.request("tool",{"action":"apply","name":"road","x1":x1,"y1":y1,"x2":x2,"y2":y2})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":9})
        await s.request("tool",{"action":"apply","name":"airport","x1":18,"y1":11})
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]["passengers_per_tick"],120)
        await s.request("time",{"action":"step","ticks":1})
        before=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        await key("F8");await key("U")
        self.assertTrue(s.last_state["ui"]["regional"])
        self.assertFalse(s.last_state["ui"]["transport"])
        await control("regional_capacity")
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(rows[0]["passengers_per_tick"],30)
        await control("regional_operation")
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertFalse(rows[0]["enabled"])
        self.assertEqual(s.last_state["metrics"]["regional_operating_cost"],0)
        await control("regional_tourism")
        after=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(after["report"],before["report"])
        self.assertEqual(after["accounting"],before["accounting"])
        await s.screenshot("m35-connections-default")
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("m35-connections-minimum")
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        for c in controls:
            self.assertGreaterEqual(c["x"],0);self.assertLessEqual(c["x"]+c["width"],800)
            self.assertGreaterEqual(c["y"],0);self.assertLessEqual(c["y"]+c["height"],640)
        await key("N")
        after=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(after["report"]["tick"],before["report"]["tick"]+1)
        self.assertEqual(after["report"]["dayArrivals"],0)
        await key("Escape");await key("U")
        # From tourism focus, eight Tabs reach operation; Enter reopens the airport.
        for _ in range(8): await key("Tab")
        await key("Enter")
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertTrue(rows[0]["enabled"])
        await control("regional_locate")
        self.assertFalse(s.last_state["ui"]["regional"])
        self.assertEqual(s.last_state["ui"]["pinned_tile"],[18,11])
        await key("U");await key("Escape")
        self.assertFalse(s.last_state["ui"]["regional"])

    async def test_regional_hotel_management_closure_recovery_and_map_focus(self):
        s=self.session
        await record(s,"start")
        await s.city("new",32)
        async def key(name):
            await s.request("input",{"events":[{"type":"key","key":name}]})
        async def control(name):
            controls=(await s.request("inspect",{"section":"ui"}))["controls"]
            c=next(c for c in controls if c["name"]==name)
            await s.request("input",{"events":[
                {"type":"down","button":"left","x":c["x"]+4,"y":c["y"]+4},
                {"type":"up","button":"left","x":c["x"]+4,"y":c["y"]+4}]})
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":8,"y1":17,"x2":9,"y2":17})
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        await s.request("time",{"action":"step","ticks":3})
        before=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertGreater(before["report"]["staying"],0)
        self.assertGreater(before["report"]["unmetOvernightBeds"],0)
        await key("U");await control("regional_alert_manage")
        self.assertTrue(s.last_state["ui"]["regional_hotel_tab"])
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        self.assertFalse(next(c for c in controls if c["name"]=="regional_capacity")["enabled"])
        await s.screenshot("m35-hotel-bed-bottleneck")
        await control("regional_operation")
        paused=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(paused,before)
        alerts=(await s.request("inspect",{"section":"regional_alerts"}))["items"]
        self.assertEqual(alerts[0]["problem"],"HOTEL BEDS")
        self.assertEqual(alerts[0]["tick"],before["report"]["tick"])
        await s.screenshot("m35-hotel-closed-before-tick")
        # A capacity action cannot modify hotel beds or worker capacity.
        await control("regional_capacity")
        await s.city("save")
        snapshot=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        hotel=next(b for b in snapshot["buildings"] if b.get("hotelBeds"))
        self.assertFalse(hotel["hotelOpen"])
        self.assertEqual(hotel["hotelBeds"],32);self.assertEqual(hotel["capacity"],12)
        await key("N")
        closed=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(closed["report"]["staying"],0)
        self.assertGreater(closed["report"]["earlyDepartures"],0)
        self.assertGreater(closed["report"]["unmetOvernightBeds"],0)
        self.assertEqual(closed["accounting"]["stayFees"],0)
        # Reload the paused closure and reproduce the exact next-tick result.
        await s.city("load")
        self.assertFalse(s.last_state["ui"]["regional"])
        await s.request("time",{"action":"step","ticks":1})
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],closed)
        await key("U");await control("regional_tab")
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("m35-hotel-minimum-closed")
        # Tab from tab-switch focus (6) reaches operation after wrapping the alert controls.
        for _ in range(7): await key("Tab")
        await key("Enter");await key("N")
        recovered=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertGreater(recovered["report"]["overnightArrivals"],0)
        self.assertGreater(recovered["accounting"]["stayFees"],0)
        for _ in range(12):
            cash=s.last_state["funds"]
            await key("N")
            tourism=(await s.request("inspect",{"section":"tourism"}))["tourism"]
            ledger=(await s.request("budget",{"action":"inspect"}))["last_tick"]
            self.assertLessEqual(tourism["report"]["staying"],32)
            self.assertEqual(s.last_state["population"],0)
            self.assertEqual(s.last_state["funds"]-cash,ledger["total_revenue"]-ledger["total_expenses"])
            self.assertEqual(ledger["tourism_fees"],tourism["accounting"]["dayFees"]+tourism["accounting"]["stayFees"])
        await s.screenshot("m35-hotel-minimum-recovered")
        await control("regional_locate")
        self.assertFalse(s.last_state["ui"]["regional"])
        self.assertEqual(s.last_state["ui"]["pinned_tile"],[8,15])
        await s.screenshot("m35-hotel-located")
        await key("U");await key("F8")
        self.assertFalse(s.last_state["ui"]["regional"])
        self.assertTrue(s.last_state["ui"]["transport"])
        scenario=await record(s,"stop","m35-hotel-closure-recovery")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_regional_mixed_access_alerts_closure_recovery_and_host_parity(self):
        s=self.session
        await s.city("new",32)
        for x1,y1,x2,y2 in ((0,10,26,10),(0,13,26,13),(17,10,17,13),(0,10,0,16)):
            await s.request("tool",{"action":"apply","name":"road","x1":x1,"y1":y1,"x2":x2,"y2":y2})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":9})
        for zone,x1,x2 in (("residential",2,8),("commercial",9,12),("industrial",13,15)):
            await s.request("tool",{"action":"apply","name":"zone","zone":zone,"x1":x1,"y1":14,"x2":x2,"y2":14})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":2,"y1":11,"x2":11,"y2":11})
        await s.request("time",{"action":"step","ticks":20})
        await s.city("save")
        fixture=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        for tile in core["tiles"]:
            if tile["y"]==8 and tile["x"]<=26: tile["type"]=2
        coast=s.artifact("mixed-base.city.json");coast.write_text(json.dumps(core))
        fixture["citySnapshot"]=str(coast);fixture["funds"]=1000000
        fixture["transitRoutes"]=[{"id":9001,"mode":0,"vehicleCount":1,
            "capacityPerVehicle":30,"stopCoverageRadius":32,
            "stops":[{"x":0,"y":10},{"x":26,"y":13}]}]
        path=s.artifact("mixed-base.json");path.write_text(json.dumps(fixture))
        await s.stop();await s.launch();await record(s,"start")
        await s.city("load",import_path=str(path))
        for name,x,y,options in (("port",22,9,{"port_action":"cargo"}),
            ("port",24,9,{"port_action":"passenger"}),("airport",18,11,{}),("hotel",8,9,{})):
            await s.request("tool",{"action":"apply","name":name,"x1":x,"y1":y,**options})
        async def key(name):
            await s.request("input",{"events":[{"type":"key","key":name}]})
        async def control(name):
            controls=(await s.request("inspect",{"section":"ui"}))["controls"]
            c=next(c for c in controls if c["name"]==name)
            await s.request("input",{"events":[
                {"type":"down","button":"left","x":c["x"]+4,"y":c["y"]+4},
                {"type":"up","button":"left","x":c["x"]+4,"y":c["y"]+4}]})
        samples=[]
        async def step(phase):
            cash=s.last_state["funds"]
            await s.request("time",{"action":"step","ticks":1})
            visitors=(await s.request("inspect",{"section":"tourism"}))["tourism"]
            r=visitors["report"]
            ledger=(await s.request("budget",{"action":"inspect"}))["last_tick"]
            cargo=await s.request("inspect",{"section":"regional_report"})
            self.assertEqual(r["dayArrivals"]+r["unmetDayTransport"],r["requestedDay"])
            self.assertEqual(r["overnightArrivals"]+r["unmetOvernightTransport"]+r["unmetOvernightBeds"],r["requestedOvernight"])
            self.assertEqual(sum(u["day"] for u in r["connections"]),r["dayArrivals"])
            self.assertEqual(sum(u["overnight"] for u in r["connections"]),r["overnightArrivals"])
            for row in visitors["travel"]["routes"]:
                self.assertLessEqual(row["residentRiders"]+row["ridership"],row["capacity"])
            self.assertEqual(s.last_state["funds"]-cash,ledger["total_revenue"]-ledger["total_expenses"])
            self.assertEqual(ledger["tourism_fees"],visitors["accounting"]["dayFees"]+visitors["accounting"]["stayFees"])
            for direction in ("imports","exports"):
                self.assertEqual(cargo["allocation"]["requested"][direction],cargo["allocation"]["admitted"][direction]+cargo["allocation"]["unmet"][direction])
                self.assertEqual(cargo["allocation"]["admitted"][direction],cargo["delivered"][direction]+cargo["undelivered"][direction])
            samples.append({"phase":phase,"tick":r["tick"],"visitors":r,"cargo":cargo,
                "travel":visitors["travel"],"ledger":ledger,"population":s.last_state["population"]})
            return visitors
        for _ in range(4): baseline=await step("mixed")
        self.assertGreater(s.last_state["population"],0)
        for kind in (0,2,3):
            self.assertGreater(sum(row["day"]+row["overnight"] for row in baseline["report"]["connections"] if row["kind"]==kind),0)
        self.assertGreater(sum(samples[-1]["cargo"]["allocation"]["requested"][k] for k in ("imports","exports")),0)
        # Closing the airport must retain the completed report/alerts until a tick.
        await s.request("tool",{"action":"apply","name":"airport","port_action":"toggle","x1":18,"y1":11})
        for _ in range(3): closed=await step("airport_closed")
        self.assertGreater(closed["report"]["unmetDayTransport"],0)
        alerts=(await s.request("inspect",{"section":"regional_alerts"}))["items"]
        self.assertEqual(alerts[0]["problem"],"VISITOR ACCESS")
        self.assertEqual(alerts[0]["target_id"],3)
        await key("U");await control("regional_alert_manage")
        self.assertEqual(s.last_state["ui"]["regional_selected"],2)
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("m35-mixed-airport-access-alert")
        await control("regional_operation")
        self.assertEqual((await s.request("inspect",{"section":"regional_alerts"}))["items"],alerts)
        recovered=await step("reopened")
        self.assertLess(recovered["report"]["unmetDayTransport"],closed["report"]["unmetDayTransport"])
        await control("regional_alert_next");await control("regional_alert_manage")
        self.assertTrue(s.last_state["ui"]["regional_hotel_tab"])
        await s.screenshot("m35-mixed-airport-recovery")
        await s.city("save")
        checkpoint=s.artifact("mixed-checkpoint.json")
        saved=json.loads(s.artifact("urban_sim_session.json").read_text())
        frozenCore=s.artifact("mixed-checkpoint.city.json")
        frozenCore.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        saved["citySnapshot"]=str(frozenCore);checkpoint.write_text(json.dumps(saved))
        for _ in range(12): await step("sustained_recovery")
        await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output=s.artifact("mixed-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","12","--simulate-session",str(checkpoint),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text());gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless)
        s.artifact("m35-mixed-measurements.json").write_text(json.dumps(samples,indent=2))
        scenario=await record(s,"stop","m35-mixed-access-recovery")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_tourism_fees_services_ledger_shutdown_and_invalid_import(self):
        s=self.session
        await record(s,"start");await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":8,"y1":17,"x2":9,"y2":17})
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        saw_bed_shortage=False
        for _ in range(4):
            cash=s.last_state["funds"]
            await s.request("time",{"action":"step","ticks":1})
            visitors=(await s.request("inspect",{"section":"tourism"}))["tourism"]
            r=visitors["report"];a=visitors["accounting"]
            saw_bed_shortage=saw_bed_shortage or r["unmetOvernightBeds"]>0
            ledger=(await s.request("budget",{"action":"inspect"}))["last_tick"]
            self.assertEqual(a["dayFees"],2*r["dayArrivals"]);self.assertEqual(a["stayFees"],4*r["staying"])
            self.assertEqual(a["services"],r["dayArrivals"]+r["staying"])
            self.assertEqual(ledger["tourism_fees"],a["dayFees"]+a["stayFees"])
            self.assertEqual(ledger["tourism_services"],a["services"])
            self.assertEqual(ledger["total_revenue"],sum(ledger["revenue"])+ledger["tourism_fees"])
            self.assertEqual(ledger["total_expenses"],sum(ledger["expenses"])+ledger["tourism_services"])
            self.assertEqual(s.last_state["funds"]-cash,ledger["total_revenue"]-ledger["total_expenses"])
            self.assertEqual(s.last_state["population"],0)
        self.assertTrue(saw_bed_shortage)
        await s.city("save");await s.city("load")
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],visitors)
        await s.request("budget",{"action":"open"})
        await s.screenshot("tourism-treasury")
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("tourism-treasury-minimum")
        await s.request("budget",{"action":"close"})
        await s.request("budget",{"action":"set_tourism","tourism_enabled":False})
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"]["accounting"],a)
        await s.request("time",{"action":"step","ticks":1})
        off=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(off["accounting"]["dayFees"],0);self.assertEqual(off["accounting"]["stayFees"],0)
        self.assertEqual(off["accounting"]["services"],0)
        self.assertGreater(off["report"]["earlyDepartures"],0)
        scenario=await record(s,"stop","tourism-municipal-accounting")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])
        await s.city("save")
        saved=json.loads(s.artifact("urban_sim_session.json").read_text())
        saved["citySnapshot"]=str(s.artifact("urban_sim_session.json.city.json"));saved["funds"]=1
        saved["tourism"]["accounting"]["stayFees"]+=1
        bad=s.artifact("bad-accounting.json");bad.write_text(json.dumps(saved));cash=s.last_state["funds"]
        with self.assertRaises(PlaytestError):
            await s.city("load",import_path=str(bad))
        self.assertEqual(s.last_state["funds"],cash)

    async def test_hotel_open_policy_departures_persistence_and_continuation(self):
        s=self.session
        await record(s,"start");await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":8,"y1":17})
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        await s.request("time",{"action":"step","ticks":1})
        hotel=(await s.request("inspect",{"section":"entities"}))["items"][0]
        before=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertGreater(before["report"]["staying"],0)
        cash=s.last_state["funds"]
        await s.request("budget",{"action":"set_hotel","hotel_id":hotel["id"],"hotel_open":False})
        self.assertEqual(s.last_state["funds"],cash)
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],before)
        for projection in ("top_down","isometric"):
            await s.request("tool",{"action":"select","name":"hotel"})
            await s.request("view",{"action":"projection","projection":projection});await s.request("view",{"action":"focus"})
            point=await s.request("coordinates",{"direction":"tile_to_screen","x":8,"y":15})
            await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
            await s.screenshot("hotel-closed-"+projection)
        await s.city("save");await s.city("load")
        closed=(await s.request("inspect",{"section":"entities"}))["items"][0]
        self.assertFalse(closed["hotel_open"]);self.assertEqual(closed["capacity"],12)
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],before)
        checkpoint=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=s.artifact("closed-checkpoint.city.json");core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        checkpoint["citySnapshot"]=str(core)
        path=s.artifact("closed-checkpoint.json");path.write_text(json.dumps(checkpoint))
        await s.request("time",{"action":"step","ticks":1})
        after=(await s.request("inspect",{"section":"tourism"}))["tourism"]["report"]
        self.assertEqual(after["staying"],0);self.assertEqual(after["overnightArrivals"],0)
        self.assertEqual(after["earlyDepartures"],before["report"]["staying"])
        self.assertGreater(after["dayArrivals"],0)
        await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        gui_core=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"));output=s.artifact("closed-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","1","--simulate-session",str(path),"--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text());gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless);self.assertEqual(gui_core,json.loads(Path(str(output)+".city.json").read_text()))
        await s.request("time",{"action":"step","ticks":1})
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"]["report"]["earlyDepartures"],0)
        # Native tile input uses the same policy setter as MCP.
        await s.request("tool",{"action":"select","name":"hotel"})
        await s.request("view",{"action":"focus"})
        point=await s.request("coordinates",{"direction":"tile_to_screen","x":8,"y":15})
        await s.request("input",{"events":[{"type":"down","button":"left","x":point["x"],"y":point["y"]},{"type":"up","button":"left","x":point["x"],"y":point["y"]}]})
        self.assertTrue((await s.request("inspect",{"section":"entities"}))["items"][0]["hotel_open"])
        await s.request("time",{"action":"step","ticks":1})
        self.assertGreater((await s.request("inspect",{"section":"tourism"}))["tourism"]["report"]["overnightArrivals"],0)
        with self.assertRaises(PlaytestError):
            await s.request("budget",{"action":"set_hotel","hotel_id":0,"hotel_open":False})
        scenario=await record(s,"stop","hotel-open-policy")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_visitor_travel_resident_priority_fleet_changes_and_continuation(self):
        s=self.session
        await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        for zone,y in (("residential",15),("commercial",17)):
            await s.request("tool",{"action":"apply","name":"zone","zone":zone,"x1":6,"y1":y,"x2":13,"y2":y})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":14,"y1":17})
        await s.request("tool",{"action":"apply","name":"hotel","x1":14,"y1":15})
        await s.request("time",{"action":"step","ticks":20});await s.city("save")
        fixture=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=s.artifact("visitor-travel-base.city.json")
        core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        fixture["citySnapshot"]=str(core)
        # Isolated route fixture; actual policies and traffic run through normal
        # gameplay actions. Historical completed usage remains unchanged.
        fixture["transitRoutes"]=[{"id":9001,"mode":0,"vehicleCount":1,
            "capacityPerVehicle":30,"stopCoverageRadius":32,
            "stops":[{"x":0,"y":16},{"x":22,"y":16}]}]
        path=s.artifact("visitor-travel-base.json");path.write_text(json.dumps(fixture))
        await s.stop();await s.launch();await record(s,"start")
        await s.city("load",import_path=str(path))
        await s.request("time",{"action":"step","ticks":1})
        measured=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        travel=measured["travel"]
        self.assertTrue(travel["measured"]);self.assertGreater(travel["requested"],0)
        resident=(await s.request("budget",{"action":"inspect"}))["transit_report"]
        self.assertGreater(resident["commuters"],0)
        row=next(r for r in travel["routes"] if r["id"]==9001)
        commuter=next(r for r in resident["routes"] if r["id"]==9001)
        self.assertEqual(row["residentRiders"],commuter["ridership"])
        self.assertLessEqual(row["residentRiders"]+row["ridership"],row["capacity"])
        self.assertEqual(travel["requested"],sum(travel[k] for k in ("road","transit","walking","unrouted")))
        await s.request("budget",{"action":"set_route_vehicles","route_id":9001,"vehicles":20})
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],measured)
        await s.request("time",{"action":"step","ticks":1})
        spare=(await s.request("inspect",{"section":"tourism"}))["tourism"]["travel"]
        self.assertGreater(spare["transit"],0);self.assertEqual(spare["road"],0)
        for r in spare["routes"]:
            self.assertLessEqual(r["residentRiders"]+r["ridership"],r["capacity"])
        await s.city("save")
        checkpoint=json.loads(s.artifact("urban_sim_session.json").read_text())
        checkpoint_core=s.artifact("visitor-travel-checkpoint.city.json")
        checkpoint_core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        checkpoint["citySnapshot"]=str(checkpoint_core)
        checkpoint_path=s.artifact("visitor-travel-checkpoint.json");checkpoint_path.write_text(json.dumps(checkpoint))
        await s.request("time",{"action":"step","ticks":6});await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        gui_core=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output=s.artifact("visitor-travel-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","6","--simulate-session",str(checkpoint_path),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text());gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless)
        def city_contents(core):
            # Newly grown entities receive process-local IDs after reload.
            positions={b["id"]:b["position"] for b in core["buildings"]}
            for building in core["buildings"]:
                building.pop("id")
            for group in core["populationGroups"]:
                group.pop("id")
            for tile in core["tiles"]:
                tile["buildingId"]=positions.get(tile["buildingId"])
            for key in ("buildings","populationGroups","roads","tiles"):
                core[key].sort(key=lambda value:json.dumps(value,sort_keys=True))
            return core
        self.assertEqual(city_contents(gui_core),city_contents(json.loads(Path(str(output)+".city.json").read_text())))
        await s.request("budget",{"action":"set_route_vehicles","route_id":9001,"vehicles":0})
        await s.request("time",{"action":"step","ticks":1})
        road_only=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(road_only["travel"]["transit"],0);self.assertGreater(road_only["travel"]["road"],0)
        edges=(await s.request("inspect",{"section":"roads"}))["items"]
        self.assertTrue(any(e["visitor_load"]>0 for e in edges))
        for e in edges:
            self.assertLessEqual(e["visitor_load"]+e["freight_load"],e["load"])
        for _ in range(2):
            await s.city("save");await s.city("load")
            self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],road_only)
            self.assertEqual((await s.request("inspect",{"section":"roads"}))["items"],edges)
        await s.request("tool",{"action":"select","name":"hotel"})
        for projection in ("top_down","isometric"):
            await s.request("view",{"action":"projection","projection":projection});await s.request("view",{"action":"focus"})
            point=await s.request("coordinates",{"direction":"tile_to_screen","x":14,"y":15})
            await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
            await s.screenshot("visitor-travel-"+projection)
        scenario=await record(s,"stop","visitor-local-travel")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_live_tourism_cohorts_policy_demolition_and_host_parity(self):
        s=self.session
        await record(s,"start");await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":8,"y1":17})
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        policy=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertTrue(policy["enabled"]);self.assertEqual(policy["report"]["tick"],0)
        await s.request("time",{"action":"step","ticks":1})
        current=(await s.request("inspect",{"section":"tourism"}))["tourism"];r=current["report"]
        self.assertEqual(r["accessibleParks"],1);self.assertEqual(r["parkContribution"],20)
        self.assertGreater(r["dayArrivals"],0);self.assertGreater(r["overnightArrivals"],0)
        self.assertEqual(r["staying"],r["overnightArrivals"])
        self.assertEqual(s.last_state["population"],0)
        self.assertEqual(s.last_state["budget"]["last_tick"]["total_revenue"],2*r["dayArrivals"]+4*r["staying"])
        cash=s.last_state["funds"];tick=s.last_state["tick"]
        for _ in range(3):
            self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],current)
        self.assertEqual(s.last_state["funds"],cash);self.assertEqual(s.last_state["tick"],tick)
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        button=next(c for c in controls if c["name"]=="tourism")
        async def toggle():
            x=button["x"]+button["width"]//2;y=button["y"]+button["height"]//2
            await s.request("input",{"events":[{"type":"move","x":x,"y":y},
                {"type":"down","button":"left","x":x,"y":y},{"type":"up","button":"left","x":x,"y":y}]})
        await toggle();off=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertFalse(off["enabled"]);self.assertEqual(off["report"],r)
        await toggle();self.assertTrue(s.last_state["metrics"]["tourism_enabled"])
        await s.city("save");await s.city("load")
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],current)
        checkpoint=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=s.artifact("tourism-checkpoint.city.json")
        core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        checkpoint["citySnapshot"]=str(core)
        path=s.artifact("tourism-checkpoint.json");path.write_text(json.dumps(checkpoint))
        await s.request("time",{"action":"step","ticks":6});await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        gui_core=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output=s.artifact("tourism-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","6","--simulate-session",str(path),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        stdout,stderr=await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        self.assertIn("TOURISM ON",stdout.decode())
        headless=json.loads(output.read_text());gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless)
        self.assertEqual(gui_core,json.loads(Path(str(output)+".city.json").read_text()))
        measured=((await s.request("inspect",{"section":"tourism"}))["tourism"])["report"]
        self.assertGreater(measured["scheduledDepartures"],0);self.assertLessEqual(measured["staying"],32)
        await s.request("tool",{"action":"select","name":"hotel"})
        for projection in ("top_down","isometric"):
            await s.request("view",{"action":"projection","projection":projection});await s.request("view",{"action":"focus"})
            point=await s.request("coordinates",{"direction":"tile_to_screen","x":8,"y":15})
            await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
            await s.screenshot("tourism-active-"+projection)
        await s.request("tool",{"action":"apply","name":"bulldoze","x1":8,"y1":15})
        pending=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        self.assertEqual(pending["state"]["stays"],[])
        self.assertEqual(pending["state"]["pendingEarlyDepartures"],measured["staying"])
        self.assertEqual(pending["report"],measured)
        await s.city("save");await s.city("load")
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],pending)
        await s.request("time",{"action":"step","ticks":1})
        after=((await s.request("inspect",{"section":"tourism"}))["tourism"])["report"]
        self.assertEqual(after["earlyDepartures"],measured["staying"])
        self.assertEqual(after["staying"],0);self.assertGreater(after["unmetOvernightBeds"],0)
        await s.request("budget",{"action":"set_tourism","tourism_enabled":False})
        await s.request("time",{"action":"step","ticks":1})
        self.assertEqual(((await s.request("inspect",{"section":"tourism"}))["tourism"])["report"]["dayArrivals"],0)
        scenario=await record(s,"stop","live-tourism-cohorts")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])
        await s.city("save")
        bad=json.loads(s.artifact("urban_sim_session.json").read_text())
        bad["citySnapshot"]=str(s.artifact("urban_sim_session.json.city.json"))
        bad["funds"]=1;bad["tourism"]["report"]["dayArrivals"]=2001
        bad_path=s.artifact("bad-tourism.json");bad_path.write_text(json.dumps(bad))
        before=(await s.request("inspect",{"section":"tourism"}))["tourism"];cash=s.last_state["funds"]
        with self.assertRaises(PlaytestError):
            await s.city("load",import_path=str(bad_path))
        self.assertEqual(s.last_state["funds"],cash)
        self.assertEqual((await s.request("inspect",{"section":"tourism"}))["tourism"],before)

    async def test_native_hotel_placement_cost_guards_persistence_and_replay(self):
        s=self.session
        await record(s,"start")
        await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        await s.request("view",{"action":"projection","projection":"top_down"})
        await s.request("view",{"action":"camera","tile_pixels":40,"view_x":3,"view_y":8})
        async def click(x,y):
            await s.request("input",{"events":[{"type":"move","x":x,"y":y},
                {"type":"down","button":"left","x":x,"y":y},{"type":"up","button":"left","x":x,"y":y}]})
        async def site(x=8,y=15):
            point=await s.request("coordinates",{"direction":"tile_to_screen","x":x,"y":y})
            await click(point["x"],point["y"])
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        button=next(c for c in controls if c["name"]=="hotel")
        cash=s.last_state["funds"]
        await click(button["x"]+button["width"]//2,button["y"]+button["height"]//2)
        self.assertTrue(s.last_state["tools"]["hotel"])
        point=await s.request("coordinates",{"direction":"tile_to_screen","x":8,"y":15})
        await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
        await s.screenshot("hotel-site-preview")
        await site()
        self.assertEqual(s.last_state["funds"],cash-10000)
        hotels=(await s.request("inspect",{"section":"entities"}))["items"]
        self.assertEqual(len(hotels),1)
        hotel=hotels[0]
        self.assertEqual(hotel["hotel_beds"],32);self.assertEqual(hotel["capacity"],12)
        self.assertEqual(hotel["occupancy"],0);self.assertEqual(hotel["type"],2)
        self.assertEqual(s.last_state["population"],0)
        await site() # Existing hotel toggles visitor operation without spending.
        self.assertFalse((await s.request("inspect",{"section":"entities"}))["items"][0]["hotel_open"])
        await site()
        self.assertEqual(s.last_state["funds"],cash-10000)
        await site(8,16) # Road tile is rejected too.
        self.assertEqual(s.last_state["funds"],cash-10000)
        await s.request("input",{"events":[{"type":"key","key":"Escape"},{"type":"key","key":"K"}]})
        self.assertTrue(s.last_state["tools"]["hotel"])
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        self.assertFalse((await s.request("inspect",{"section":"entities"}))["items"][0]["hotel_open"])
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        self.assertEqual(s.last_state["funds"],cash-10000)
        with self.assertRaises(PlaytestError):
            await s.request("tool",{"action":"select","name":"hotel","capacity":1})
        for projection in ("top_down","isometric"):
            await s.request("view",{"action":"projection","projection":projection})
            await s.request("view",{"action":"focus"})
            point=await s.request("coordinates",{"direction":"tile_to_screen","x":8,"y":15})
            await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
            await s.screenshot("hotel-native-"+projection)
        await s.city("save");await s.city("load")
        restored=(await s.request("inspect",{"section":"entities"}))["items"]
        self.assertEqual(restored,hotels)
        self.assertEqual(s.last_state["funds"],cash-10000)
        await s.request("tool",{"action":"apply","name":"bulldoze","x1":8,"y1":15})
        self.assertEqual(s.last_state["funds"],cash-10200)
        self.assertEqual((await s.request("inspect",{"section":"entities"}))["items"],[])
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":15})
        newer=(await s.request("inspect",{"section":"entities"}))["items"][0]
        self.assertGreater(newer["id"],hotel["id"])
        self.assertEqual(s.last_state["funds"],cash-20200)
        self.assertEqual(newer["hotel_beds"],32)
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("hotel-native-minimum-window")
        scenario=await record(s,"stop","native-hotel-placement")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_hotel_metadata_session_continuation_and_replay(self):
        s = self.session
        await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        await s.city("save")
        fixture = json.loads(s.artifact("urban_sim_session.json").read_text())
        core = json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        # Isolated saved fixture representing HotelTool output; this does not
        # claim native hotel placement or completed tourism integration.
        core["buildings"].append({"id":101,"type":2,"position":{"x":8,"y":15},
            "capacity":12,"occupancy":0,"hotelBeds":32,"crimeRate":0.0,"illnessRate":0.0})
        core["nextEntityId"] = 102
        next(t for t in core["tiles"] if t["x"]==8 and t["y"]==15)["buildingId"] = 101
        core_path = s.artifact("hotel-base.city.json");core_path.write_text(json.dumps(core))
        fixture["citySnapshot"] = str(core_path);fixture["funds"] -= 10000
        path = s.artifact("hotel-base.json");path.write_text(json.dumps(fixture))
        await s.stop();await s.launch();await record(s,"start")
        await s.city("load",import_path=str(path))
        cash = s.last_state["funds"]
        await s.city("save");await s.city("load")
        self.assertEqual(s.last_state["funds"],cash)
        self.assertEqual(s.last_state["population"],0)
        checkpoint = json.loads(s.artifact("urban_sim_session.json").read_text())
        checkpoint_core = s.artifact("hotel-checkpoint.city.json")
        checkpoint_core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        self.assertEqual(json.loads(checkpoint_core.read_text())["buildings"][0]["hotelBeds"],32)
        checkpoint["citySnapshot"] = str(checkpoint_core)
        checkpoint_path = s.artifact("hotel-checkpoint.json");checkpoint_path.write_text(json.dumps(checkpoint))
        await s.request("time",{"action":"step","ticks":10});await s.city("save")
        gui = json.loads(s.artifact("urban_sim_session.json").read_text())
        gui_core = json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        self.assertEqual(gui_core["buildings"][0]["hotelBeds"],32)
        self.assertEqual(gui_core["buildings"][0]["capacity"],12)
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output = s.artifact("hotel-headless.json")
        process = await asyncio.create_subprocess_exec(str(cli),"--simulate","10", "--simulate-session",str(checkpoint_path),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr = await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        headless = json.loads(output.read_text())
        gui.pop("citySnapshot");headless.pop("citySnapshot");self.assertEqual(gui,headless)
        self.assertEqual(gui_core,json.loads(Path(str(output)+".city.json").read_text()))
        scenario = await record(s,"stop","hotel-metadata-continuation")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_native_airport_controls_readiness_without_tourism_and_host_parity(self):
        s=self.session
        await s.city("new",32)
        for x1,y1,x2,y2 in ((0,10,26,10),(17,13,26,13),(17,10,17,13)):
            await s.request("tool",{"action":"apply","name":"road","x1":x1,"y1":y1,"x2":x2,"y2":y2})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":9})
        await s.city("save")
        fixture=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=s.artifact("airport-base.city.json");core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        fixture["citySnapshot"]=str(core);fixture["funds"]=100000 # Isolated acceptance liquidity.
        path=s.artifact("airport-base.json");path.write_text(json.dumps(fixture))
        await s.stop();await s.launch();await record(s,"start")
        await s.city("load",import_path=str(path))
        await s.request("view",{"action":"projection","projection":"top_down"})
        await s.request("view",{"action":"camera","tile_pixels":40,"view_x":10,"view_y":5})
        async def key(name):
            await s.request("input",{"events":[{"type":"key","key":name}]})
        async def click(x,y):
            await s.request("input",{"events":[{"type":"move","x":x,"y":y},
                {"type":"down","button":"left","x":x,"y":y},{"type":"up","button":"left","x":x,"y":y}]})
        async def site(x=18,y=11):
            p=await s.request("coordinates",{"direction":"tile_to_screen","x":x,"y":y})
            await click(p["x"],p["y"])
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        button=next(c for c in controls if c["name"]=="airport")
        await click(button["x"]+button["width"]//2,button["y"]+button["height"]//2)
        self.assertEqual(s.last_state["tools"]["port_action"],5)
        await site()
        self.assertEqual(s.last_state["funds"],75000)
        self.assertFalse(s.last_state["metrics"]["regional_logistics_enabled"])
        self.assertEqual(s.last_state["metrics"]["airport_operating_cost"],30)
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(rows[0]["kind"],"airport");self.assertEqual(rows[0]["available_passengers_per_tick"],120)
        self.assertEqual(rows[0]["goods_per_tick"],0);self.assertEqual(rows[0]["site_access"],"ready")
        tiles=(await s.request("inspect",{"section":"tiles","x":18,"y":11,"width":4,"height":2}))["items"]
        self.assertEqual([t["regional_connection_id"] for t in tiles],[1]*8)
        await key("Escape");await key("J")
        self.assertEqual(s.last_state["tools"]["port_action"],5)
        await s.request("tool",{"action":"select","name":"airport","port_action":"capacity"})
        await site(21,12)
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(rows[0]["passengers_per_tick"],30)
        await s.request("tool",{"action":"apply","name":"airport","port_action":"capacity","capacity":90,"x1":18,"y1":11})
        await s.request("tool",{"action":"select","name":"airport","port_action":"toggle"});await site()
        self.assertEqual(s.last_state["metrics"]["airport_operating_cost"],0)
        await s.request("time",{"action":"step","ticks":1})
        self.assertEqual(s.last_state["funds"],74927)
        measured=await s.request("inspect",{"section":"regional_report"})
        self.assertEqual(measured["allocation"]["connections"][0]["availablePassengers"],0)
        self.assertEqual(measured["delivered"]["passengers"],0)
        await s.city("save");await s.city("load")
        await s.request("tool",{"action":"select","name":"airport","port_action":"toggle"});await site()
        await s.request("time",{"action":"step","ticks":1})
        self.assertEqual(s.last_state["funds"],74824)
        measured=await s.request("inspect",{"section":"regional_report"})
        self.assertEqual(measured["allocation"]["connections"][0]["availablePassengers"],90)
        self.assertEqual(measured["operatingExpense"],30);self.assertEqual(s.last_state["population"],0)
        self.assertFalse(s.last_state["metrics"]["regional_logistics_enabled"])
        for projection in ("top_down","isometric"):
            await s.request("view",{"action":"projection","projection":projection})
            await s.request("view",{"action":"focus"})
            p=await s.request("coordinates",{"direction":"tile_to_screen","x":18,"y":11})
            await s.request("input",{"events":[{"type":"move","x":p["x"],"y":p["y"]}]})
            await s.screenshot("airport-"+projection)
        await s.city("save")
        checkpoint=json.loads(s.artifact("urban_sim_session.json").read_text())
        checkpoint_core=s.artifact("airport-checkpoint.city.json")
        checkpoint_core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        checkpoint["citySnapshot"]=str(checkpoint_core)
        checkpoint_path=s.artifact("airport-checkpoint.json");checkpoint_path.write_text(json.dumps(checkpoint))
        await s.request("time",{"action":"step","ticks":10});await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"));output=s.artifact("airport-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","10","--simulate-session",str(checkpoint_path),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text());gui.pop("citySnapshot");headless.pop("citySnapshot");self.assertEqual(gui,headless)
        scenario=await record(s,"stop","native-airport-controls")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])
        cash=s.last_state["funds"]
        with self.assertRaises(PlaytestError):
            await s.request("tool",{"action":"apply","name":"airport","port_action":"capacity","capacity":121,"x1":18,"y1":11})
        self.assertEqual(s.last_state["funds"],cash)
        await s.request("tool",{"action":"select","name":"airport","port_action":"remove"});await site(21,12)
        self.assertEqual(s.last_state["funds"],cash-1000)
        self.assertFalse(s.last_state["metrics"]["regional_logistics_enabled"])
        await key("J");await site()
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(rows[0]["id"],2)
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.request("view",{"action":"focus"})
        await s.screenshot("airport-minimum-window")

    async def test_native_port_tools_prices_controls_persistence_and_replay(self):
        s=self.session
        await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":10,"x2":25,"y2":10})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":9})
        await s.city("save")
        fixture=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        for tile in core["tiles"]:
            if tile["y"]==8 and tile["x"]<=25:tile["type"]=2
        coast=s.artifact("native-ports.city.json");coast.write_text(json.dumps(core))
        fixture["citySnapshot"]=str(coast)
        fixture["funds"]=100000 # Liquidity for this isolated acceptance fixture only.
        path=s.artifact("native-ports.json");path.write_text(json.dumps(fixture))
        await s.stop();await s.launch();await record(s,"start")
        await s.city("load",import_path=str(path))
        await s.request("view",{"action":"projection","projection":"top_down"})
        await s.request("view",{"action":"camera","tile_pixels":48,"view_x":6,"view_y":3})
        async def key(name):
            await s.request("input",{"events":[{"type":"key","key":name}]})
        async def click(x,y):
            await s.request("input",{"events":[{"type":"move","x":x,"y":y},
                {"type":"down","button":"left","x":x,"y":y},{"type":"up","button":"left","x":x,"y":y}]})
        async def site(x,y):
            point=await s.request("coordinates",{"direction":"tile_to_screen","x":x,"y":y})
            await click(point["x"],point["y"])
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        button=next(c for c in controls if c["name"]=="ports")
        await click(button["x"]+button["width"]//2,button["y"]+button["height"]//2)
        self.assertTrue(s.last_state["tools"]["port"])
        await site(18,9)
        self.assertEqual(s.last_state["funds"],85000)
        self.assertTrue(s.last_state["metrics"]["regional_logistics_enabled"])
        self.assertEqual(s.last_state["tick"],0)
        toast=s.last_state["toast"]
        await click(20,s.last_state["dimensions"]["height"]-145)
        self.assertEqual(s.last_state["toast"],toast) # Hint panel cannot place behind itself.
        await s.city("save")
        self.assertTrue(json.loads(s.artifact("urban_sim_session.json").read_text())["regionalLogisticsEnabled"])
        await key("V");await site(22,9)
        self.assertEqual(s.last_state["funds"],75000)
        await s.screenshot("native-ports-top_down")
        await key("F3");await s.request("view",{"action":"focus"})
        point=await s.request("coordinates",{"direction":"tile_to_screen","x":22,"y":9})
        await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
        await s.screenshot("native-ports-isometric")
        await key("V");await site(18,9) # Whole cargo port, before any tick.
        self.assertEqual(s.last_state["funds"],74500)
        self.assertTrue(s.last_state["metrics"]["regional_logistics_enabled"])
        await key("V");await site(22,9) # Disable passenger upkeep.
        await key("V");await site(22,9) # Cycle passenger capacity to 15.
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(len(rows),1);self.assertEqual(rows[0]["id"],2)
        self.assertFalse(rows[0]["enabled"]);self.assertEqual(rows[0]["passengers_per_tick"],15)
        await key("V");await site(18,9) # New cargo gets ID 3, never reused 1.
        self.assertEqual(s.last_state["funds"],59500)
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual([c["id"] for c in rows],[2,3])
        await key("Escape")
        self.assertFalse(s.last_state["tools"]["port"])
        await s.city("save");await s.city("load")
        saved=json.loads(s.artifact("urban_sim_session.json").read_text())
        self.assertEqual(saved["nextRegionalConnectionId"],4)
        checkpoint=s.artifact("native-ports-checkpoint.json")
        checkpoint_core=s.artifact("native-ports-checkpoint.city.json")
        checkpoint_core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        saved["citySnapshot"]=str(checkpoint_core);checkpoint.write_text(json.dumps(saved))
        await s.request("time",{"action":"step","ticks":20})
        self.assertEqual(s.last_state["metrics"]["port_operating_cost"],20)
        await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output=s.artifact("native-ports-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","20","--simulate-session",str(checkpoint),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate();self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text());gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless)
        scenario=await record(s,"stop","native-port-tools")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])
        cash=s.last_state["funds"]
        for options in ({"port_action":"cargo"},{"port_action":"capacity","capacity":61}):
            with self.assertRaises(PlaytestError):
                await s.request("tool",{"action":"apply","name":"port","x1":22,"y1":9,**options})
            self.assertEqual(s.last_state["funds"],cash)
        await s.request("tool",{"action":"apply","name":"port","port_action":"capacity","capacity":30,"x1":22,"y1":9})
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual(rows[0]["passengers_per_tick"],30)
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.request("view",{"action":"focus"})
        point=await s.request("coordinates",{"direction":"tile_to_screen","x":22,"y":9})
        await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
        await s.screenshot("native-ports-minimum-window")

    async def test_constructed_port_reservations_edit_guards_and_continuation(self):
        s=self.session
        await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":0,"y1":10,"x2":25,"y2":10})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":9})
        for zone,x1,x2 in (("residential",1,8),("commercial",20,25)):
            await s.request("tool",{"action":"apply","name":"zone","zone":zone,"x1":x1,"y1":11,"x2":x2,"y2":11})
        await s.request("time",{"action":"step","ticks":30})
        await s.city("save")
        legacy=json.loads(s.artifact("urban_sim_session.json").read_text())
        legacy.pop("regionalConnections",None)
        city=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        for tile in city["tiles"]:
            if tile["y"]==8 and tile["x"]<=20:
                tile["type"]=2
        core=s.artifact("port-checkpoint.city.json");core.write_text(json.dumps(city))
        legacy["citySnapshot"]=str(core)
        fixture=dict(legacy)
        fixture["funds"]-=15000 # Already-paid historical fixture; API payment is tested in C++.
        fixture["regionalConnections"]=[{"id":17,"kind":1,"x":18,"y":9,"width":2,"height":1,
            "entranceX":18,"entranceY":10,"enabled":True,"constructed":True,
            "passengersPerTick":0,"goodsPerTick":200}]
        path=s.artifact("port-fixture.json");path.write_text(json.dumps(fixture))
        await s.city("load",import_path=str(path))
        tiles=(await s.request("inspect",{"section":"tiles","x":18,"y":9,"width":2,"height":1}))["items"]
        self.assertEqual([t["regional_connection_id"] for t in tiles],[17,17])
        port=(await s.request("inspect",{"section":"regional_connections"}))["items"][0]
        self.assertTrue(port["constructed"])
        self.assertEqual(port["site_access"],"ready")
        cash=s.last_state["funds"]
        for tool,extra in (("road",{"x2":20,"y2":9}),("zone",{"zone":"residential"}),
                           ("service",{"service":"fire"}),("bulldoze",{})):
            with self.assertRaises(PlaytestError):
                await s.request("tool",{"action":"apply","name":tool,"x1":18,"y1":9,**extra})
            self.assertEqual(s.last_state["funds"],cash)
        # Colliding saved terrain must reject before replacing the live city.
        bad_city=json.loads(json.dumps(city))
        for tile in bad_city["tiles"]:
            if tile["x"]==18 and tile["y"]==9:
                tile["zone"]=1
        bad_core=s.artifact("port-invalid.city.json");bad_core.write_text(json.dumps(bad_city))
        bad=dict(fixture,citySnapshot=str(bad_core))
        bad_path=s.artifact("port-invalid.json");bad_path.write_text(json.dumps(bad))
        with self.assertRaises(PlaytestError):
            await s.city("load",import_path=str(bad_path))
        self.assertEqual(s.last_state["funds"],cash)
        self.assertEqual((await s.request("inspect",{"section":"tiles","x":18,"y":9}))["items"][0]["regional_connection_id"],17)
        await s.city("save")
        await s.request("time",{"action":"step","ticks":20})
        self.assertTrue(s.last_state["metrics"]["regional_logistics_enabled"])
        self.assertEqual(s.last_state["metrics"]["port_operating_cost"],20)
        measured=await s.request("inspect",{"section":"regional_report"})
        self.assertGreater(measured["delivered"]["imports"],0)
        self.assertEqual(measured["operatingExpense"],20)
        self.assertEqual(measured["amountScale"],0.01)
        gateways=s.last_state["metrics"]["regional_gateways"]
        self.assertEqual([row["goods_per_tick"] for row in gateways],[200,100])
        await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        output=s.artifact("port-headless.json")
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","20",
            "--simulate-session",str(path),"--simulate-session-out",str(output),
            stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate()
        self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text())
        gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless)
        await s.city("load",import_path=str(output))
        self.assertEqual((await s.request("inspect",{"section":"tiles","x":18,"y":9}))["items"][0]["regional_connection_id"],17)
        old=s.artifact("port-legacy.json");old.write_text(json.dumps(legacy))
        await s.city("load",import_path=str(old))
        self.assertEqual((await s.request("inspect",{"section":"tiles","x":18,"y":9}))["items"][0]["regional_connection_id"],0)
        await s.stop();await s.launch();await record(s,"start")
        await s.city("load",import_path=str(path))
        await s.request("time",{"action":"step","ticks":20})
        await assert_state(s,"items.0.regional_connection_id","eq",17,section="tiles",query={"x":18,"y":9})
        await s.city("save");await s.city("load")
        scenario=await record(s,"stop","constructed-port-reservations")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_regional_configuration_inspection_persistence_and_host_parity(self):
        s=self.session
        await s.city("new",32)
        await s.city("save")
        legacy=json.loads(s.artifact("urban_sim_session.json").read_text())
        legacy.pop("regionalConnections",None)
        legacy.pop("regionalSettlement",None)
        core=s.artifact("regional-checkpoint.city.json")
        core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
        legacy["citySnapshot"]=str(core)
        fixture=dict(legacy)
        fixture["regionalConnections"]=[
            {"id":8,"kind":3,"x":20,"y":20,"width":2,"height":2,
             "entranceX":20,"entranceY":22,"enabled":True,
             "passengersPerTick":60,"goodsPerTick":0},
            {"id":2,"kind":0,"x":0,"y":16,"width":1,"height":1,
             "entranceX":0,"entranceY":16,"enabled":False,
             "passengersPerTick":10,"goodsPerTick":100}]
        fixture["tick"]=3
        fixture.pop("tourism",None) # Deliberate pre-tourism historical checkpoint.
        zero={"passengers":0,"imports":0,"exports":0}
        fixture["regionalSettlement"]={"valid":True,"allocation":{"tick":3,
            "requested":dict(zero,passengers=10),"admitted":dict(zero,passengers=10),"unmet":zero,
            "connections":[{"id":8,"access":0,"passengers":10,"imports":0,"exports":0}]},
            "deliveries":[{"id":8,"delivered":dict(zero,passengers=5)}],
            "delivered":dict(zero,passengers=5),"undelivered":dict(zero,passengers=5),
            "prices":{"passengerFee":2,"importCost":0,"exportRevenue":0,"operatingCost":3},
            "revenue":10,"expenses":3}
        path=s.artifact("regional-fixture.json");path.write_text(json.dumps(fixture))
        await s.city("load",import_path=str(path))
        before=s.last_state.copy()
        rows=(await s.request("inspect",{"section":"regional_connections"}))["items"]
        self.assertEqual([row["id"] for row in rows],[2,8])
        self.assertEqual(rows[1]["kind"],"airport")
        self.assertEqual(rows[1]["passengers_per_tick"],60)
        self.assertEqual([row["site_access"] for row in rows],["not constructed","not constructed"])
        measured=await s.request("inspect",{"section":"regional_report"})
        self.assertTrue(measured["valid"])
        self.assertEqual(measured["revenue"],10)
        self.assertEqual(measured["delivered"]["passengers"],5)
        for field in ("tick","funds","population","budget"):
            self.assertEqual(s.last_state[field],before[field])
        page=await s.request("inspect",{"section":"regional_connections","offset":1,"limit":1})
        self.assertEqual(page["items"],[rows[1]])
        bad=dict(fixture);bad["regionalConnections"]=[dict(fixture["regionalConnections"][0],width=100)]
        badpath=s.artifact("regional-invalid.json");badpath.write_text(json.dumps(bad))
        with self.assertRaises(PlaytestError):
            await s.city("load",import_path=str(badpath))
        self.assertEqual((await s.request("inspect",{"section":"regional_connections"}))["items"],rows)
        bad=json.loads(json.dumps(fixture));bad["regionalSettlement"]["revenue"]=11
        badpath.write_text(json.dumps(bad))
        with self.assertRaises(PlaytestError):
            await s.city("load",import_path=str(badpath))
        self.assertEqual((await s.request("inspect",{"section":"regional_report"}))["revenue"],10)
        await s.city("save")
        self.assertEqual(json.loads(s.artifact("urban_sim_session.json").read_text())["regionalConnections"][0]["id"],2)
        await s.request("time",{"action":"step","ticks":20})
        await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        self.assertEqual(gui["funds"],before["funds"])
        output=s.artifact("regional-headless.json")
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","20",
            "--simulate-session",str(path),"--simulate-session-out",str(output),
            stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await process.communicate()
        self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text())
        gui.pop("citySnapshot");headless.pop("citySnapshot")
        self.assertEqual(gui,headless)
        await s.city("load",import_path=str(output))
        self.assertEqual((await s.request("inspect",{"section":"regional_connections"}))["items"],rows)
        old=s.artifact("regional-legacy.json");old.write_text(json.dumps(legacy))
        await s.city("load",import_path=str(old))
        self.assertEqual((await s.request("inspect",{"section":"regional_connections"}))["items"],[])
        self.assertFalse((await s.request("inspect",{"section":"regional_report"}))["valid"])
        await s.stop()
        await s.launch()
        await record(s,"start")
        await s.city("load",import_path=str(path))
        await s.request("time",{"action":"step","ticks":20})
        await assert_state(s,"items.1.id","eq",8,section="regional_connections")
        await s.city("save")
        await s.city("load")
        scenario=await record(s,"stop","regional-configuration")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_service_funding_controls_coverage_bills_and_continuation(self):
        s = self.session
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 15), ("water", 17)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        await s.request("tool", {"action": "apply", "name": "service", "service": "fire", "x1": 22, "y1": 15})
        before = s.last_state.copy()
        self.assertGreater(before["budget"]["service_coverage"][0], 0)
        await s.request("budget", {"action": "services"})
        layout = await s.request("inspect", {"section": "ui"})
        button = next(c for c in layout["controls"] if c["name"] == "funding_0_minus")
        for _ in range(2):
            await s.request("input", {"events": [{"type": "down", "x": button["x"]+2, "y": button["y"]+2},
                                                 {"type": "up", "x": button["x"]+2, "y": button["y"]+2}]})
        self.assertEqual(s.last_state["budget"]["service_percent"][0], 50)
        self.assertEqual(s.last_state["budget"]["service_costs"][0], 13)
        self.assertLess(s.last_state["budget"]["service_coverage"][0], before["budget"]["service_coverage"][0])
        for key in ("funds", "tick", "population", "debt"):
            self.assertEqual(s.last_state[key], before[key], key)
        self.assertEqual(s.last_state["budget"]["last_tick"], before["budget"]["last_tick"])
        taxes = [7,9,8,10,11]
        await s.request("budget", {"action": "set_taxes", "tax_percent": taxes})
        self.assertEqual(s.last_state["budget"]["service_percent"][0], 50)
        funding = [50,100,100,100,0,100,100,100,100,100,100,100]
        await s.request("budget", {"action": "set_funding", "service_percent": funding})
        self.assertEqual(s.last_state["budget"]["tax_percent"], taxes)
        self.assertEqual(s.last_state["budget"]["service_costs"][4], 0)
        self.assertEqual(s.last_state["budget"]["service_coverage"][4], 0)
        self.assertEqual(s.last_state["metrics"]["power_supply_mw"], 0)
        self.assertEqual(s.last_state["facilities"], 3)
        for invalid in ([100]*10, [101]*11, [-1]*11, [50.5]*11):
            with self.assertRaises(PlaytestError):
                await s.request("budget", {"action": "set_funding", "service_percent": invalid})
        await s.request("input", {"events": [{"type": "move", "x": 0, "y": 0}]})
        await s.screenshot("funding-services")
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.screenshot("funding-minimum-window")
        await s.city("save")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core_copy = s.artifact("funding-checkpoint.city.json")
        core_copy.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core_copy)
        checkpoint = s.artifact("funding-checkpoint.json")
        checkpoint.write_text(json.dumps(frozen))
        await s.request("time", {"action": "step", "ticks": 2})
        self.assertEqual(s.last_state["budget"]["last_tick"]["expenses"][3], 41)
        await s.city("save")
        gui = json.loads(s.artifact("urban_sim_session.json").read_text())
        output = s.artifact("funding-headless.json")
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest", "-cli"))
        process = await asyncio.create_subprocess_exec(str(cli), "--simulate", "2",
            "--simulate-session", str(checkpoint), "--simulate-session-out", str(output),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await process.communicate()
        self.assertEqual(process.returncode, 0, stderr.decode())
        headless = json.loads(output.read_text())
        for key in ("funds", "tick", "treasuryDebt", "treasuryLedger", "budgetTaxPercent", "budgetServicePercent", "migrationConditions", "populationChange"):
            self.assertEqual(headless[key], gui[key], key)
        await s.city("load", import_path=str(output))
        self.assertEqual(s.last_state["budget"]["service_percent"], funding)
        self.assertEqual(s.last_state["metrics"]["power_supply_mw"], 0)
        await s.request("budget", {"action": "reset_funding"})
        self.assertEqual(s.last_state["budget"]["tax_percent"], taxes)
        self.assertEqual(s.last_state["budget"]["service_percent"], [100]*12)
        self.assertEqual(s.last_state["metrics"]["power_supply_mw"], 100)
        self.assertEqual(s.last_state["budget"]["service_coverage"][0], 1)
        self.assertEqual(s.last_state["facilities"], 3)

    async def test_tax_pressure_changes_growth_and_survives_low_tax_save(self):
        s = self.session
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 15), ("water", 17)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        await s.city("save")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core = s.artifact("tax-branch.city.json")
        core.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core)
        checkpoint = s.artifact("tax-branch.json")
        checkpoint.write_text(json.dumps(frozen))
        populations = []
        for rate in (0, 20):
            await s.city("load", import_path=str(checkpoint))
            before = s.last_state.copy()
            await s.request("budget", {"action": "set_taxes", "tax_percent": [rate]*5})
            self.assertEqual(s.last_state["population"], before["population"])
            await s.request("time", {"action": "step", "ticks": 12})
            populations.append(s.last_state["population"])
            await s.city("save")
            await s.city("load")
            self.assertEqual(s.last_state["population"], populations[-1])
            self.assertEqual(s.last_state["budget"]["tax_percent"], [rate]*5)
        self.assertGreater(populations[0], populations[1])

    async def test_trends_history_and_headless_continuation(self):
        s = self.session
        await s.city("new", 32)
        self.assertEqual(s.last_state["budget"]["history"], [])
        await s.request("budget", {"action": "trends"})
        await s.screenshot("trends-empty")
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 15), ("water", 17)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        initial = s.last_state["budget"]["history"]
        self.assertEqual(len(initial), 20)
        self.assertGreater(initial[-1]["population"], 0)
        await s.request("budget", {"action": "set_taxes", "tax_percent": [12,8,6,9,10]})
        self.assertEqual(s.last_state["budget"]["history"], initial)
        await s.request("time", {"action": "step", "ticks": 3})
        history = s.last_state["budget"]["history"]
        self.assertEqual([r["tick"] for r in history if r["policy_changed"]], [21])
        self.assertEqual(history[-1]["revenue"], s.last_state["budget"]["last_tick"]["total_revenue"])
        self.assertEqual(history[-1]["funds"], s.last_state["funds"])
        await s.request("budget", {"action": "taxes"})
        layout = await s.request("inspect", {"section": "ui"})
        button = next(c for c in layout["controls"] if c["name"] == "budget_trends")
        await s.request("input", {"events": [{"type": "down", "x": button["x"]+2, "y": button["y"]+2},
                                             {"type": "up", "x": button["x"]+2, "y": button["y"]+2}]})
        self.assertTrue(s.last_state["ui"]["budget_trends"])
        controls = (await s.request("inspect", {"section": "ui"}))["controls"]
        self.assertEqual({c["name"] for c in controls}, {"budget_close", "budget_taxes", "budget_services", "budget_trends", "budget_alerts"})
        await s.screenshot("trends-normal")
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.screenshot("trends-minimum-window")
        await s.city("save")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core = s.artifact("trends-branch.city.json")
        core.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core)
        checkpoint = s.artifact("trends-branch.json")
        checkpoint.write_text(json.dumps(frozen))
        await s.request("time", {"action": "step", "ticks": 2})
        await s.city("save")
        gui = json.loads(s.artifact("urban_sim_session.json").read_text())
        output = s.artifact("trends-headless.json")
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest", "-cli"))
        process = await asyncio.create_subprocess_exec(str(cli), "--simulate", "2",
            "--simulate-session", str(checkpoint), "--simulate-session-out", str(output),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await process.communicate()
        self.assertEqual(process.returncode, 0, stderr.decode())
        headless = json.loads(output.read_text())
        self.assertEqual(headless["cityHistory"], gui["cityHistory"])
        await s.city("load", import_path=str(output))
        self.assertEqual(s.last_state["budget"]["history"], gui["cityHistory"])
        await s.city("new", 32)
        self.assertEqual(s.last_state["budget"]["history"], [])

    async def test_city_alerts_diagnostics_actions_and_continuation(self):
        s = self.session
        await s.city("new",32)
        await s.request("budget", {"action":"alerts"})
        self.assertEqual(s.last_state["budget"]["alerts"]["checked_tick"],0)
        await s.screenshot("alerts-unchecked")
        await s.request("budget", {"action":"close"})
        await s.request("tool", {"action":"apply","name":"road","x1":6,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool", {"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        for zone,y in (("residential",15),("commercial",17)):
            await s.request("tool", {"action":"apply","name":"zone","zone":zone,"x1":6,"y1":y,"x2":13,"y2":y})
        await s.request("time", {"action":"step","ticks":20})
        before = s.last_state["budget"]["alerts"]
        funding = [100]*11
        funding[4] = funding[5] = 0
        await s.request("budget", {"action":"set_funding","service_percent":funding})
        self.assertEqual(s.last_state["budget"]["alerts"],before)
        await s.request("time", {"action":"step","ticks":3})
        alerts = s.last_state["budget"]["alerts"]
        self.assertEqual(alerts["checked_tick"],23)
        self.assertTrue(alerts["records"][0]["active"])
        self.assertTrue(alerts["records"][1]["active"])
        self.assertEqual(alerts["records"][0]["last_notice_tick"],23)
        await s.request("input", {"events":[{"type":"key","key":"F7"}]})
        self.assertTrue(s.last_state["ui"]["budget_alerts"])
        await s.screenshot("alerts-normal")
        await s.request("window", {"action":"resize","width":800,"height":640})
        await s.screenshot("alerts-minimum-window")
        controls = (await s.request("inspect", {"section":"ui"}))["controls"]
        button = next(c for c in controls if c["name"] == "alert_view_0")
        await s.request("input", {"events":[{"type":"down","x":button["x"]+2,"y":button["y"]+2},
                                             {"type":"up","x":button["x"]+2,"y":button["y"]+2}]})
        self.assertFalse(s.last_state["ui"]["budget"])
        self.assertEqual(s.last_state["overlay"],3)
        await s.request("budget", {"action":"view_alert","alert_index":2})
        self.assertEqual(s.last_state["overlay"],5)
        await s.request("budget", {"action":"view_alert","alert_index":4})
        self.assertTrue(s.last_state["ui"]["budget"])
        self.assertFalse(s.last_state["ui"]["budget_alerts"])
        await s.city("save")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core = s.artifact("alerts-branch.city.json")
        core.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core)
        checkpoint = s.artifact("alerts-branch.json")
        checkpoint.write_text(json.dumps(frozen))
        await s.city("load",import_path=str(checkpoint))
        self.assertEqual(s.last_state["budget"]["alerts"],alerts)
        await s.request("time", {"action":"step","ticks":2})
        self.assertEqual(s.last_state["budget"]["alerts"]["records"][0]["last_notice_tick"],23)
        await s.city("save")
        gui = json.loads(s.artifact("urban_sim_session.json").read_text())
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output = s.artifact("alerts-headless.json")
        process = await asyncio.create_subprocess_exec(str(cli),"--simulate","2",
            "--simulate-session",str(checkpoint),"--simulate-session-out",str(output),
            stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        stdout,stderr = await process.communicate()
        self.assertEqual(process.returncode,0,stderr.decode())
        headless = json.loads(output.read_text())
        self.assertEqual(headless["cityAlerts"],gui["cityAlerts"])
        await s.city("load",import_path=str(output))
        await s.request("budget", {"action":"reset_funding"})
        await s.request("time", {"action":"step","ticks":2})
        self.assertFalse(s.last_state["budget"]["alerts"]["records"][0]["active"])
        self.assertFalse(s.last_state["budget"]["alerts"]["records"][1]["active"])

    async def test_building_diagnostics_are_read_only_and_survive_load(self):
        s = self.session
        await s.city("new",32)
        await s.request("tool", {"action":"apply","name":"zone","zone":"residential","x1":6,"y1":15})
        blocked = await s.request("inspect", {"section":"diagnostics","x":6,"y":15})
        self.assertIn("no_road",blocked["growth"]["blockers"])
        await s.request("tool", {"action":"apply","name":"road","x1":6,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool", {"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        for zone,y in (("residential",15),("commercial",17)):
            await s.request("tool", {"action":"apply","name":"zone","zone":zone,"x1":6,"y1":y,"x2":13,"y2":y})
        await s.request("time", {"action":"step","ticks":20})
        entities = (await s.request("inspect", {"section":"entities"}))["items"]
        home = next(b for b in entities if b["type"] == 1)
        args = {"section":"diagnostics","x":home["x"],"y":home["y"]}
        before = s.last_state.copy()
        diagnosis = await s.request("inspect",args)
        self.assertTrue(diagnosis["growth"]["occupied"])
        self.assertEqual(diagnosis["building"]["occupancy"],home["occupancy"])
        self.assertGreater(diagnosis["neighborhood"]["buildings"],0)
        self.assertEqual(diagnosis["neighborhood"]["tiles"],81)
        for field in ("tick","funds","population","budget"):
            self.assertEqual(s.last_state[field],before[field],field)
        with self.assertRaises(PlaytestError):
            await s.request("inspect", {"section":"diagnostics","x":-1,"y":0})
        await s.city("save")
        await s.city("load")
        restored = await s.request("inspect",args)
        for field in ("x","y","tick","growth","building","services_mask","neighborhood"):
            self.assertEqual(restored[field],diagnosis[field],field)
        await s.request("view", {"action":"projection","projection":"top_down"})
        await s.request("view", {"action":"camera","tile_pixels":48,
            "view_x":max(0,home["x"]-6),"view_y":max(0,home["y"]-8)})
        point = await s.request("coordinates", {"direction":"tile_to_screen","x":home["x"],"y":home["y"]})
        await s.request("input", {"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
        await s.screenshot("diagnostics-normal")
        await s.request("window", {"action":"resize","width":800,"height":640})
        await s.screenshot("diagnostics-minimum-window")

    async def test_transport_fleets_billing_usage_replay_and_continuation(self):
        s=self.session
        await record(s,"start")
        await s.city("new",32)
        await s.request("budget",{"action":"transport"})
        await s.screenshot("transport-empty")
        await s.request("budget",{"action":"close"})
        await s.request("tool",{"action":"apply","name":"road","x1":6,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        for zone,y in (("residential",15),("commercial",17)):
            await s.request("tool",{"action":"apply","name":"zone","zone":zone,"x1":6,"y1":y,"x2":13,"y2":y})
        await s.request("time",{"action":"step","ticks":20})
        await s.city("save")
        fixture=json.loads(s.artifact("urban_sim_session.json").read_text())
        fixture["transitRoutes"]=[{"id":99,"mode":0,"vehicleCount":2,"capacityPerVehicle":30,
            "stopCoverageRadius":24,"stops":[{"x":6,"y":16},{"x":22,"y":16}]}]
        injected=s.artifact("transport-fixture.json")
        injected.write_text(json.dumps(fixture))
        await s.city("load",import_path=str(injected))
        await s.request("time",{"action":"step","ticks":1})
        prior=s.last_state.copy()
        report=prior["budget"]["transit_report"]
        self.assertGreater(report["routes"][0]["ridership"],0)
        await s.request("input",{"events":[{"type":"key","key":"F8"}]})
        self.assertTrue(s.last_state["ui"]["transport"])
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        button=next(c for c in controls if c["name"]=="vehicles_plus")
        await s.request("input",{"events":[{"type":"down","x":button["x"]+2,"y":button["y"]+2},
                                            {"type":"up","x":button["x"]+2,"y":button["y"]+2}]})
        route=(await s.request("inspect",{"section":"routes"}))["items"][0]
        self.assertEqual(route["vehicle_count"],3)
        self.assertEqual(route["operating_cost"],6)
        self.assertEqual(s.last_state["budget"]["transit_report"],report)
        self.assertEqual(s.last_state["funds"],prior["funds"])
        await s.screenshot("transport-normal")
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("transport-minimum-window")
        controls=(await s.request("inspect",{"section":"ui"}))["controls"]
        button=next(c for c in controls if c["name"]=="route_bottleneck")
        await s.request("input",{"events":[{"type":"down","x":button["x"]+2,"y":button["y"]+2},
                                            {"type":"up","x":button["x"]+2,"y":button["y"]+2}]})
        self.assertEqual(s.last_state["overlay"],4)
        self.assertFalse(s.last_state["ui"]["transport"])
        for invalid in (-1,21,1.5):
            with self.assertRaises(PlaytestError):
                await s.request("budget",{"action":"set_route_vehicles","route_id":99,"vehicles":invalid})
        await s.request("budget",{"action":"set_route_vehicles","route_id":99,"vehicles":0})
        await s.request("time",{"action":"step","ticks":1})
        usage=s.last_state["budget"]["transit_report"]["routes"][0]
        self.assertEqual(usage["ridership"],0)
        self.assertGreater(usage["demand"],0)
        self.assertEqual(s.last_state["budget"]["last_tick"]["expenses"][3],sum(s.last_state["budget"]["service_costs"]))
        await s.request("budget",{"action":"set_route_vehicles","route_id":99,"vehicles":3})
        await s.city("save")
        frozen=json.loads(s.artifact("urban_sim_session.json").read_text())
        core=s.artifact("transport-checkpoint.city.json")
        core.write_bytes(Path(frozen["citySnapshot"]).read_bytes());frozen["citySnapshot"]=str(core)
        checkpoint=s.artifact("transport-checkpoint.json");checkpoint.write_text(json.dumps(frozen))
        await s.request("time",{"action":"step","ticks":2})
        self.assertEqual(s.last_state["budget"]["last_tick"]["expenses"][3],sum(s.last_state["budget"]["service_costs"])+6)
        await s.city("save")
        gui=json.loads(s.artifact("urban_sim_session.json").read_text())
        cli=Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest","-cli"))
        output=s.artifact("transport-headless.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","2","--simulate-session",str(checkpoint),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        stdout,stderr=await process.communicate()
        self.assertEqual(process.returncode,0,stderr.decode())
        headless=json.loads(output.read_text())
        for key in ("transitRoutes","transitReport","treasuryLedger","funds","cityHistory"):
            self.assertEqual(headless[key],gui[key],key)
        await s.city("load",import_path=str(output))
        self.assertEqual((await s.request("inspect",{"section":"routes"}))["items"][0]["vehicle_count"],3)
        scenario=await record(s,"stop","transport-fleet")
        self.assertTrue((await replay(s,scenario["scenario"],2))["identical"])

    async def test_management_keyboard_chart_details_and_alert_targeting(self):
        s=self.session
        async def key(name):
            await s.request("input",{"events":[{"type":"key","key":name}]})
        async def focus(name):
            for _ in range(60):
                controls=(await s.request("inspect",{"section":"ui"}))["controls"]
                if any(c["name"]==name and c.get("focused") for c in controls):return
                await key("Tab")
            self.fail("Could not focus "+name)
        await s.city("new",32)
        await s.request("tool",{"action":"apply","name":"road","x1":6,"y1":16,"x2":22,"y2":16})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":15})
        for zone,y in (("residential",15),("commercial",17)):
            await s.request("tool",{"action":"apply","name":"zone","zone":zone,"x1":6,"y1":y,"x2":13,"y2":y})
        await s.request("time",{"action":"step","ticks":20})
        await s.request("budget",{"action":"set_taxes","tax_percent":[12,8,6,9,10]})
        await s.request("budget",{"action":"set_funding","service_percent":[50]+[100]*10})
        await s.request("time",{"action":"step","ticks":3})
        before=s.last_state.copy()
        await key("F6")
        await focus("budget_trends");await key("Enter")
        self.assertTrue(s.last_state["ui"]["budget_trends"])
        charts=(await s.request("inspect",{"section":"ui"}))["charts"]
        self.assertEqual(len(charts),6)
        c=charts[0]
        x=c["x"]+round(20*(c["width"]-1)/22)
        await s.request("input",{"events":[{"type":"move","x":x,"y":c["y"]+20}]})
        self.assertEqual(s.last_state["ui"]["trends_tick"],21)
        await s.screenshot("management-trend-detail")
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.screenshot("management-minimum-window")
        large=s.last_state["ui"]["large_text"]
        await key("F2");self.assertNotEqual(s.last_state["ui"]["large_text"],large)
        await key("F2");self.assertEqual(s.last_state["ui"]["large_text"],large)
        await key("Home");self.assertEqual(s.last_state["ui"]["trends_tick"],1)
        await key("End");self.assertEqual(s.last_state["ui"]["trends_tick"],23)
        await key("Left");self.assertEqual(s.last_state["ui"]["trends_tick"],22)
        for field in ("tick","funds","population","budget"):
            self.assertEqual(s.last_state[field],before[field],field)
        await key("Escape")
        await s.city("save")
        fixture=json.loads(s.artifact("urban_sim_session.json").read_text())
        fixture["transitRoutes"]=[{"id":99,"mode":0,"vehicleCount":2,"capacityPerVehicle":30,
            "stopCoverageRadius":24,"stops":[{"x":6,"y":16},{"x":22,"y":16}]}]
        path=s.artifact("management-fixture.json");path.write_text(json.dumps(fixture))
        await s.city("load",import_path=str(path))
        await key("F8")
        await focus("vehicles_plus");await key("Enter")
        self.assertEqual((await s.request("inspect",{"section":"routes"}))["items"][0]["vehicle_count"],3)
        await s.screenshot("management-transport-focus")
        await key("F7")
        self.assertTrue(s.last_state["ui"]["budget_alerts"])
        funding=[100]*11;funding[4]=0
        await s.request("budget",{"action":"set_funding","service_percent":funding})
        await s.request("time",{"action":"step","ticks":3})
        await focus("alert_view_0");await key("Enter")
        target=s.last_state["ui"]["pinned_tile"]
        self.assertNotEqual(target,[-1,-1])
        self.assertEqual(s.last_state["overlay"],3)
        diagnosis=await s.request("inspect",{"section":"diagnostics","x":target[0],"y":target[1]})
        self.assertFalse(diagnosis["growth"]["power"])
        await s.screenshot("management-alert-target")
        await key("I");self.assertEqual(s.last_state["ui"]["pinned_tile"],[-1,-1])
        point=await s.request("coordinates",{"direction":"tile_to_screen","x":target[0],"y":target[1]})
        await s.request("input",{"events":[{"type":"move","x":point["x"],"y":point["y"]}]})
        await key("I");self.assertEqual(s.last_state["ui"]["pinned_tile"],target)
        await key("Escape");self.assertEqual(s.last_state["ui"]["pinned_tile"],[-1,-1])
        self.assertEqual(s.last_state["phase"],"gameplay")

    async def test_budget_policy_records_and_replays(self):
        s = self.session
        await record(s, "start")
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "zone", "zone": "park", "x1": 8, "y1": 12})
        await s.request("budget", {"action": "open"})
        await s.request("budget", {"action": "set_taxes", "tax_percent": [12, 8, 6, 9, 10]})
        await s.request("time", {"action": "step", "ticks": 2})
        await s.request("budget", {"action": "reset"})
        await s.request("budget", {"action": "set_taxes", "tax_percent": [7, 9, 8, 10, 11]})
        await s.request("budget", {"action": "close"})
        await s.city("save")
        await s.city("load")
        await s.request("budget", {"action": "set_funding", "service_percent": [50]*11})
        await assert_state(s, "state.budget.service_percent.0", "eq", 50)
        await assert_state(s, "state.budget.tax_percent.0", "eq", 7)
        await assert_state(s, "state.budget.last_tick.tick", "eq", 2)
        scenario = await record(s, "stop", "budget-policy")
        self.assertTrue((await replay(s, scenario["scenario"], 2))["identical"])

    async def test_m23_budget_controls_preview_history_and_headless(self):
        s = self.session
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        for service, x in (("power", 15), ("water", 17)):
            await s.request("tool", {"action": "apply", "name": "service", "service": service, "x1": x, "y1": 15})
        for zone, y in (("residential", 15), ("commercial", 17)):
            await s.request("tool", {"action": "apply", "name": "zone", "zone": zone, "x1": 6, "y1": y, "x2": 13, "y2": y})
        await s.request("time", {"action": "step", "ticks": 20})
        initial = s.last_state.copy()
        self.assertGreater(initial["population"], 0)
        self.assertTrue(initial["budget"]["last_tick"]["valid"])
        await s.request("input", {"events": [{"type": "key", "key": "F6"}]})
        self.assertTrue(s.last_state["ui"]["budget"])
        layout = await s.request("inspect", {"section": "ui"})
        plus = next(c for c in layout["controls"] if c["name"] == "tax_0_plus")
        await s.request("input", {"events": [{"type": "down", "x": plus["x"] + 4, "y": plus["y"] + 4},
                                             {"type": "up", "x": plus["x"] + 4, "y": plus["y"] + 4}]})
        self.assertEqual(s.last_state["budget"]["tax_percent"], [6, 8, 6, 9, 10])
        for key in ("funds", "tick", "debt", "population"):
            self.assertEqual(s.last_state[key], initial[key], key)
        self.assertEqual(s.last_state["budget"]["last_tick"], initial["budget"]["last_tick"])
        await s.request("budget", {"action": "set_taxes", "tax_percent": [0, 0, 0, 0, 0]})
        self.assertEqual(s.last_state["budget"]["preview"]["total_revenue"], 0)
        self.assertTrue(all(v > 1 for v in s.last_state["budget"]["tax_attraction"]))
        await s.request("budget", {"action": "set_taxes", "tax_percent": [20, 20, 20, 20, 20]})
        self.assertGreater(s.last_state["budget"]["preview"]["total_revenue"], 0)
        self.assertTrue(all(v < 1 for v in s.last_state["budget"]["tax_attraction"]))
        self.assertEqual(s.last_state["population"], initial["population"])
        preview = s.last_state["budget"]["preview"]
        self.assertEqual(sum(preview["revenue"]), preview["total_revenue"])
        self.assertEqual(sum(preview["expenses"]), preview["total_expenses"])
        self.assertEqual(preview["expenses"][4], 0)
        for invalid in ([21, 8, 6, 9, 10], [-1, 8, 6, 9, 10], [5, 8]):
            with self.assertRaises(PlaytestError):
                await s.request("budget", {"action": "set_taxes", "tax_percent": invalid})
        await s.request("input", {"events": [{"type": "move", "x": 0, "y": 0}]})
        await s.screenshot("budget-normal")
        await s.request("window", {"action": "resize", "width": 800, "height": 640})
        await s.screenshot("budget-minimum-window")
        await s.city("save")
        await s.city("load")
        self.assertEqual(s.last_state["budget"]["tax_percent"], [20] * 5)
        self.assertEqual(s.last_state["budget"]["last_tick"], initial["budget"]["last_tick"])
        await s.city("save")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core_copy = s.artifact("budget-checkpoint.city.json")
        core_copy.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core_copy)
        checkpoint = s.artifact("budget-checkpoint.json")
        checkpoint.write_text(json.dumps(frozen))
        await s.request("time", {"action": "step", "ticks": 1})
        await s.city("save")
        gui = json.loads(s.artifact("urban_sim_session.json").read_text())
        ledger = s.last_state["budget"]["last_tick"]
        self.assertEqual(ledger["funds_before"] + ledger["total_revenue"] -
            ledger["total_expenses"] + ledger["borrowed"] - ledger["repaid"], ledger["funds_after"])
        self.assertEqual(sum(ledger["revenue"]), ledger["total_revenue"])
        self.assertEqual(sum(ledger["expenses"]), ledger["total_expenses"])
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest", "-cli"))
        output = s.artifact("budget-headless.json")
        process = await asyncio.create_subprocess_exec(str(cli), "--simulate", "1",
            "--simulate-session", str(checkpoint), "--simulate-session-out", str(output),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await process.communicate()
        self.assertEqual(process.returncode, 0, stderr.decode())
        headless = json.loads(output.read_text())
        for key in ("funds", "tick", "treasuryDebt", "treasuryLedger", "budgetTaxPercent", "populationChange", "demand", "cityHistory"):
            self.assertEqual(headless[key], gui[key], key)
        await s.request("budget", {"action": "open"})
        await s.request("budget", {"action": "reset"})
        self.assertEqual(s.last_state["budget"]["tax_percent"], [5, 8, 6, 9, 10])
        await s.request("input", {"events": [{"type": "key", "key": "Escape"}]})
        self.assertFalse(s.last_state["ui"]["budget"])

    async def test_m22_saved_city_debt_and_disasters_match_headless(self):
        s = self.session
        await s.city("new", 32)
        self.assertFalse(s.last_state["disasters"]["enabled"])
        await s.request("view", {"action": "toggle", "name": "compact_ui", "enabled": False})
        # Exercise the visible opt-in control as well as the semantic toggle.
        layout = await s.request("inspect", {"section": "ui"})
        button = next(b for b in layout["controls"] if b["name"] == "disasters")
        x, y = button["x"] + 4, button["y"] + 4
        await s.request("input", {"events": [{"type": "down", "x": x, "y": y},
                                                {"type": "up", "x": x, "y": y}]})
        self.assertTrue(s.last_state["disasters"]["enabled"])
        await s.request("tool", {"action": "apply", "name": "road", "x1": 6, "y1": 16, "x2": 22, "y2": 16})
        await s.request("tool", {"action": "apply", "name": "service", "service": "power", "x1": 15, "y1": 15})
        await s.request("tool", {"action": "apply", "name": "service", "service": "water", "x1": 17, "y1": 15})
        await s.request("tool", {"action": "apply", "name": "zone", "zone": "residential", "x1": 6, "y1": 15, "x2": 13, "y2": 15})
        await s.request("tool", {"action": "apply", "name": "zone", "zone": "commercial", "x1": 6, "y1": 17, "x2": 13, "y2": 17})
        await s.request("time", {"action": "step", "ticks": 20})
        await s.city("save")
        saved = json.loads(s.artifact("urban_sim_session.json").read_text())
        saved.update(funds=0, treasuryDebt=250, treasuryInterestRemainder=0.75,
                     simulationSeed=12345)
        saved["disasters"] = {"enabled": True,
            "burningTiles": [{"x": 14, "y": 17, "ticksRemaining": 4}],
            "buildingsLostToFire": 3, "buildingsLostToDisaster": 2}
        fixture = s.artifact("m22-input.json")
        fixture.write_text(json.dumps(saved))
        await s.city("load", import_path=str(fixture))
        self.assertEqual(s.last_state["debt"], 250)
        self.assertEqual(s.last_state["disasters"]["active_fires"], 1)
        await s.city("save")
        # Preserve exactly the same starting checkpoint for both hosts.
        checkpoint = s.artifact("m22-checkpoint.json")
        frozen = json.loads(s.artifact("urban_sim_session.json").read_text())
        core_copy = s.artifact("m22-checkpoint.city.json")
        core_copy.write_bytes(Path(frozen["citySnapshot"]).read_bytes())
        frozen["citySnapshot"] = str(core_copy)
        checkpoint.write_text(json.dumps(frozen))
        await s.request("time", {"action": "step", "ticks": 2})
        expected = s.last_state.copy()
        await s.city("save")
        gui = json.loads(s.artifact("urban_sim_session.json").read_text())
        gui_core = json.loads(Path(gui["citySnapshot"]).read_text())
        cli = Path(EXECUTABLE).with_name(Path(EXECUTABLE).name.replace("-playtest", "-cli"))
        output = s.artifact("m22-headless.json")
        process = await asyncio.create_subprocess_exec(str(cli), "--simulate", "2",
            "--simulate-session", str(checkpoint), "--simulate-session-out", str(output),
            "--simulate-report", str(s.artifact("m22.csv")),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await process.communicate()
        self.assertEqual(process.returncode, 0, stderr.decode())
        self.assertIn("DEBT", stdout.decode())
        headless = json.loads(output.read_text())
        for field in ("funds", "tick", "treasuryDebt", "treasuryInterestRemainder", "disasters",
                      "populationChange", "migrationConditions"):
            self.assertEqual(headless[field], gui[field], field)
        def city_contents(core):
            # IDs are process-local; compare all semantic city data, including
            # each tile's building reference resolved to its position.
            positions = {b["id"]: b["position"] for b in core["buildings"]}
            for building in core["buildings"]:
                building.pop("id")
            for group in core["populationGroups"]:
                group.pop("id")
            for tile in core["tiles"]:
                tile["buildingId"] = positions.get(tile["buildingId"])
            for key in ("buildings", "populationGroups", "roads", "tiles"):
                core[key].sort(key=lambda value: json.dumps(value, sort_keys=True))
            return core
        self.assertEqual(city_contents(json.loads(Path(headless["citySnapshot"]).read_text())),
                         city_contents(gui_core))
        await s.city("load", import_path=str(output))
        for field in ("funds", "debt", "tick", "population", "disasters"):
            self.assertEqual(s.last_state[field], expected[field], field)
        await s.request("input", {"events": [{"type": "move", "x": 0, "y": 0}]})
        for projection in ("isometric", "top_down"):
            await s.request("view", {"action": "projection", "projection": projection})
            await s.request("view", {"action": "focus"})
            await s.screenshot("m22-disasters-" + projection)
        await s.request("view", {"action": "toggle", "name": "disasters", "enabled": False})
        self.assertFalse(s.last_state["disasters"]["enabled"])
        self.assertEqual(s.last_state["disasters"]["active_fires"], 0)
        self.assertGreaterEqual(s.last_state["disasters"]["lost_fire"], 3)

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

    async def test_parks_cost_upkeep_persistence_and_projection(self):
        s = self.session
        await record(s, "start")
        await s.city("new", 32)
        await s.request("tool", {"action": "apply", "name": "zone", "zone": "park",
                                 "x1": 8, "y1": 12, "x2": 9, "y2": 13})
        self.assertEqual(s.last_state["funds"], 49600)
        self.assertEqual(s.last_state["park_tiles"], 4)
        self.assertEqual(s.last_state["park_upkeep"], 8)
        await s.request("tool", {"action": "apply", "name": "zone", "zone": "residential",
                                 "x1": 10, "y1": 12, "x2": 10, "y2": 12})
        tiles = await s.request("inspect", {"section": "tiles", "x": 10, "y": 12})
        self.assertGreater(tiles["items"][0]["land_value"], 120)
        await s.request("time", {"action": "step", "ticks": 3})
        self.assertEqual(s.last_state["funds"], 49551)
        self.assertEqual(s.last_state["buildings"], 0)
        for projection in ("top_down", "isometric"):
            await s.request("view", {"action": "projection", "projection": projection})
            await s.request("view", {"action": "focus"})
            await s.screenshot("park-" + projection)
        await s.city("save")
        await s.city("load")
        self.assertEqual(s.last_state["park_tiles"], 4)
        self.assertEqual(s.last_state["funds"], 49551)
        await s.request("tool", {"action": "apply", "name": "bulldoze",
                                 "x1": 8, "y1": 12, "x2": 9, "y2": 13})
        self.assertEqual(s.last_state["park_tiles"], 0)
        self.assertEqual(s.last_state["funds"], 49531)
        await s.request("time", {"action": "step", "ticks": 1})
        self.assertEqual(s.last_state["expenses"], 0)
        self.assertEqual(s.last_state["funds"], 49531)
        scenario = await record(s, "stop", "parks")
        self.assertTrue((await replay(s, scenario["scenario"], 2))["identical"])

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
            for overlay in range(9):
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
        await s.request("input", {"events": [{"type": "key", "key": "Num9"}]})
        self.assertEqual(s.last_state["overlay"], 8)
        await s.request("view", {"action": "overlay", "overlay": 0})
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
        # This acceptance scenario isolates migration/transit and persistence
        # from fiscal tuning. Fund its large service/grid setup explicitly;
        # balanced-economy tests cover normal starting cash separately.
        await s.city("save")
        funded = json.loads(s.artifact("urban_sim_session.json").read_text())
        funded["funds"] = 250000
        fixture = s.artifact("migration-funded.json")
        fixture.write_text(json.dumps(funded))
        await s.city("load", import_path=str(fixture))
        self.assertEqual(s.last_state["funds"], 250000)
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
        await s.request("view", {"action": "overlay", "overlay": 8})
        for projection in ("isometric", "top_down"):
            await s.request("view", {"action": "projection", "projection": projection})
            await s.request("view", {"action": "focus"})
            await s.screenshot("transit-" + projection)
        await s.request("view", {"action": "projection", "projection": "isometric"})
        await s.request("view", {"action": "overlay", "overlay": 0})

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
        self.assertEqual(after["capacity"], 400)
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
                self.assertEqual(len((await client.list_tools()).tools), 17)
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
                changed = await client.call_tool("budget", {
                    "action": "set_taxes", "tax_percent": [6, 8, 6, 9, 10]})
                self.assertFalse(changed.isError)
                self.assertEqual(changed.structuredContent["tax_percent"], [6, 8, 6, 9, 10])
                funded = await client.call_tool("budget", {"action": "set_funding", "service_percent": [50]*11})
                self.assertFalse(funded.isError)
                self.assertEqual(funded.structuredContent["service_percent"], [50]*11 + [100])
                self.assertFalse((await client.call_tool("budget", {"action": "reset"})).isError)
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
                        e["road_class"] == "arterial" and e["capacity"] == 400
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
