"""Balanced fresh-city journey with mandatory water/sewer networks: native purchases, normal funds and catalog gates.

This is a fixed playthrough, not a budget-playing agent. Named stages wait for
their milestone and purchase reserve. All construction, policy and time changes
use native input. The original m55_native_journey.py remains unchanged.
"""
import argparse
import asyncio
import csv
import json
import shutil
import hashlib
import re
import math
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record
from urban_sim_mcp.paced_input import PacedInput
from urban_sim_mcp.street_layout import StreetLayout, runs


class Journey:
    def __init__(self, args):
        self.args = args
        self.s = Session(args.executable, args.artifacts, timeout=120)
        self.rows = {}
        self.samples = []
        self.decisions = []
        self.stage = "opening"
        self.player = PacedInput(self.s)
        self.focus = (48,48)
        self.preview_stages = set()
        self.streets = StreetLayout(args.road_profile)

    async def keys(self, *names):
        if not self.args.calibration and any(k != "N" for k in names):
            for name in names: await self.player.key(name)
            return
        await self.s.request("input", {"events": [{"type": "key", "key": key} for key in names]})

    async def click(self, x, y):
        if not self.args.calibration:
            await self.player.click(x,y)
            return
        await self.s.request("input", {"events": [
            {"type": "move", "x": x, "y": y},
            {"type": "down", "button": "left", "x": x, "y": y},
            {"type": "up", "button": "left", "x": x, "y": y}]})

    async def control(self, name):
        controls = (await self.s.request("inspect", {"section": "ui"}))["controls"]
        c = next(c for c in controls if c["name"] == name)
        await self.click(c["x"] + c["width"] // 2, c["y"] + c["height"] // 2)

    async def at(self, p):
        self.focus = p
        if not self.args.calibration:
            await self.player.frame_tile(*p)
        point = await self.s.request("coordinates", {"direction": "tile_to_screen", "x": p[0], "y": p[1]})
        assert 0 <= point["x"] < 1280 and 0 <= point["y"] < 740, (p, point)
        return {"x": point["x"], "y": point["y"]}

    async def drag(self, a, b):
        self.focus = ((a[0]+b[0])//2,(a[1]+b[1])//2)
        if self.args.calibration:
            p,q = await self.at(a),await self.at(b)
            await self.s.request("input", {"events":[{"type":"move",**p},
                {"type":"down","button":"left",**p},{"type":"move",**q},
                {"type":"up","button":"left",**q}]})
            return
        await self.player.moderate_zoom(24)
        await self.player.frame_tile(*self.focus)
        points=[]
        for tile in (a,b):
            p=await self.s.request("coordinates",{"direction":"tile_to_screen","x":tile[0],"y":tile[1]})
            points.append((p["x"],p["y"]))
        async def preview():
            tools=self.s.last_state["tools"]
            name=next(k for k in ("road","pipe","zone") if tools[k])
            assert self.s.last_state["preview"][name]["valid"],self.s.last_state["preview"]
            if self.stage not in self.preview_stages:
                await self.player.pause(1.2)
                await self.s.screenshot("preview-"+re.sub(r"[^A-Za-z0-9_.-]+", "-", self.stage))
                self.preview_stages.add(self.stage)
        await self.player.drag(*points,seconds=1.3,on_hold=preview)

    def decision(self, action, before, **detail):
        v = self.s.last_state
        self.decisions.append({"tick": v["tick"], "population": v["population"], "stage": self.stage,
            "action": action, "funds_before": before, "funds_after": v["funds"], **detail})
        self.s.artifact("decisions.json").write_text(json.dumps(self.decisions, indent=2))

    async def checkpoint(self, name):
        if not self.args.calibration:
            if any(self.s.last_state["tools"][k] for k in ("road","pipe","zone","service","district","bulldoze")):
                await self.keys("Escape")
            await self.player.moderate_zoom(30)
            p=await self.player.frame_tile(*self.focus)
            await self.player.orbit(p,dx=50,dy=-4)
            await self.player.pause(2)
            await self.s.screenshot(name)
        await self.keys("F5")
        core = self.s.artifact(name + ".city.json")
        shutil.copyfile(self.s.artifact("urban_sim_session.json.city.json"), core)
        session = json.loads(self.s.artifact("urban_sim_session.json").read_text())
        session["citySnapshot"] = str(core)
        path = self.s.artifact(name + ".json")
        path.write_text(json.dumps(session, indent=2))
        snapshot=json.loads(core.read_text())
        tiles={(t["x"],t["y"]):t for t in snapshot["tiles"]}
        for road in self.streets.records:
            for point in road["path"]:
                utility=tiles[tuple(point)]["utility"]
                assert utility["waterPipe"] and utility["sewerPipe"],("Roadside pipe gap",point)
        graph={}
        for edge in snapshot["roads"]:
            a=(edge["from"]["x"],edge["from"]["y"]);b=(edge["to"]["x"],edge["to"]["y"])
            graph.setdefault(a,set()).add(b);graph.setdefault(b,set()).add(a)
        reached=set();pending=[next(iter(graph))] if graph else []
        while pending:
            node=pending.pop()
            if node not in reached:reached.add(node);pending.extend(graph[node]-reached)
        assert len(reached)==len(graph),("Disconnected streets",name,len(reached),len(graph))
        path.with_suffix(".roads.json").write_text(json.dumps(self.streets.summary(),indent=2))
        return path

    async def step(self, n=100):
        assert n > 0
        for offset in range(0, n, 100):
            await self.keys(*(["N"] * min(100, n-offset)))
            v = self.s.last_state
            assert not v["autonomous_growth"]
            assert v["debt"] == 0 and v["funds"] >= 0, (self.stage, v["tick"], v["funds"], v["debt"])
            for row in v["budget"]["history"]:
                self.rows[row["tick"]] = row
            sample = {key: v[key] for key in ("tick", "population", "funds", "debt", "revenue", "expenses", "buildings", "facilities")}
            sample.update(stage=self.stage, uniques=sum(item["built"] for item in v["budget"]["unique_buildings"]),
                          power=v["metrics"]["power_supply_ratio"], water=v["metrics"]["water_supply_ratio"])
            self.samples.append(sample)
            self.s.artifact("measurements.json").write_text(json.dumps(self.samples, indent=2))
            with self.s.artifact("money.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=("tick", "population", "funds", "debt", "revenue", "expenses"), extrasaction="ignore")
                writer.writeheader()
                writer.writerows(self.rows[tick] for tick in sorted(self.rows))
            print(json.dumps(sample), flush=True)
            if v["tick"] == 12000:
                await self.checkpoint("tick-12000")

    async def wait(self, label, population=0, funds=0):
        while self.s.last_state["population"] < population or self.s.last_state["funds"] < funds:
            assert self.s.last_state["tick"] < self.args.max_ticks, (label, self.s.last_state["population"], self.s.last_state["funds"])
            await self.step(1 if population == 10000 and self.s.last_state["population"] >= 8000 else 100)

    async def road_mode(self, layout, arterial):
        if not self.s.last_state["tools"]["road"]: await self.keys("R")
        if (self.s.last_state["tools"]["road_class"]=="arterial") != arterial: await self.keys("A")
        name="CURVE" if layout=="curve" else "GRID DRAG"
        if layout=="grid" and not self.s.last_state["tools"]["road_snap"]: await self.keys("O")
        for _ in range(4):
            if self.s.last_state["tools"]["road_layout"]==name:break
            await self.keys("L")
        assert self.s.last_state["tools"]["road_layout"]==name
        if layout=="curve" and self.s.last_state["tools"]["road_snap"]: await self.keys("O")

    async def curved_street(self, a, b, arterial):
        # A real three-click freeform Bezier, with a visible bend. Try the other
        # side if occupied land blocks it; never silently substitute a grid road.
        dx,dy=b[0]-a[0],b[1]-a[1]
        length=math.hypot(dx,dy)
        for bend in (3,-3,2,-2):
            await self.road_mode("curve",arterial)
            control=(round((a[0]+b[0])/2-dy/length*bend),
                     round((a[1]+b[1])/2+dx/length*bend))
            before=self.s.last_state["funds"]
            for tile in (a,control):
                point=await self.at(tile)
                await self.click(point["x"],point["y"])
            point=await self.at(b)
            if self.args.calibration:
                await self.s.request("input",{"events":[{"type":"move",**point}]})
            else: await self.player.move(point["x"],point["y"])
            quote=self.s.last_state["preview"]["road"]
            if not quote["valid"]:
                error=self.s.last_state["preview"]["road_error"]
                await self.keys("Escape")
                assert self.s.last_state["funds"]==before
                if error=="insufficient funds": await self.wait("curved street reserve",funds=quote["cost"]+5000)
                continue
            if not self.args.calibration:
                await self.player.pause(1.2)
                await self.s.screenshot("curve-preview-"+str(len(self.streets.records)))
            await self.click(point["x"],point["y"])
            assert before-self.s.last_state["funds"]==quote["cost"]
            await self.keys("F5")
            snapshot=json.loads(self.s.artifact("urban_sim_session.json.city.json").read_text())
            return snapshot["roadAlignments"][-1]
        raise AssertionError(("No valid curved street",a,b,self.s.last_state["preview"]))

    async def road(self, a, b, arterial=False):
        previous=self.streets.existing(a,b)
        layout=previous["layout"] if previous else self.streets.next_layout()
        # The itinerary upgrades its original grid street, without adding a slot.
        if previous: assert layout=="grid", "curved upgrades need their saved path"
        pa,pb=self.streets.site(a),self.streets.site(b)
        unit=self.s.last_state["budget"]["construction_prices"]["arterial_segment" if arterial else "local_segment"]
        cost=(abs(pa[0]-pb[0])+abs(pa[1]-pb[1])+12)*unit
        await self.wait("street purchase reserve",funds=cost+5000)
        before=self.s.last_state["funds"]
        if layout=="curve":
            alignment=await self.curved_street(pa,pb,arterial)
            path=[(v["x"],v["y"]) for v in alignment["path"]]
            curve=alignment["curves"][0]
        else:
            await self.road_mode("grid",arterial)
            await self.drag(pa,pb)
            path=[pa]
            while path[-1]!=pb:
                x,y=path[-1]
                path.append((x+(1 if pb[0]>x else -1),y) if x!=pb[0] else (x,y+(1 if pb[1]>y else -1)))
            curve=None
        assert self.s.last_state["funds"]<before,(a,b,self.s.last_state["toast"])
        if not previous:
            self.streets.records.append({"start":a,"end":b,"physical_start":pa,"physical_end":pb,
                "layout":layout,"curve":curve,"path":path,"tick":self.s.last_state["tick"]})
        self.s.artifact("road-mix.json").write_text(json.dumps(self.streets.summary(),indent=2))
        self.decision("road",before,start=a,end=b,arterial=arterial,layout=layout,upgrade=bool(previous))
        for start,end in runs(path): await self.pipes(start,end,physical=True)

    async def pipes(self,a,b,kind=3,remove=False,physical=False):
        if not physical: a,b=self.streets.site(a),self.streets.site(b)
        region={"section":"tiles","x":min(a[0],b[0]),"y":min(a[1],b[1]),
                "width":abs(a[0]-b[0])+1,"height":abs(a[1]-b[1])+1,"limit":512}
        cells=(await self.s.request("inspect",region))["items"]
        if not remove and kind<=3 and all((kind&1==0 or t["utility"]["waterPipe"]) and
            (kind&2==0 or t["utility"]["sewerPipe"]) for t in cells): return
        cost=(2500 if kind==4 else 3000 if kind==5 else len(cells)*70)
        await self.wait("pipe purchase reserve",funds=cost+1000)
        if not self.s.last_state["tools"]["pipe"]: await self.keys("X")
        if kind==4 and self.s.last_state["tools"]["pipe_kind"]!=4: await self.keys("L")
        elif kind==5 and self.s.last_state["tools"]["pipe_kind"]!=5: await self.keys("A")
        elif kind<=3:
            for _ in range(4):
                if self.s.last_state["tools"]["pipe_kind"]==kind: break
                await self.keys("X")
        if self.s.last_state["tools"]["pipe_remove"]!=remove: await self.keys("V")
        before=self.s.last_state["funds"]
        await self.drag(a,b)
        assert self.s.last_state["funds"]<before,(a,b,self.s.last_state["toast"])
        self.decision("pipe",before,start=a,end=b,kind=kind,remove=remove)

    async def zone(self, kind, a, b):
        desired={self.streets.site((x,y)) for y in range(a[1],b[1]+1) for x in range(a[0],b[0]+1)}
        x0,y0=min(x for x,y in desired),min(y for x,y in desired)
        x1,y1=max(x for x,y in desired),max(y for x,y in desired)
        cells=(await self.s.request("inspect",{"section":"tiles","x":x0,"y":y0,
            "width":x1-x0+1,"height":y1-y0+1,"limit":512}))["items"]
        sites={(f["x"],f["y"]) for f in (await self.s.request("inspect",{"section":"facilities","limit":512}))["items"]}
        eligible={(t["x"],t["y"]) for t in cells if (t["x"],t["y"]) in desired and
            not t["road"] and t["type"]!=2 and not t["regional_connection_id"] and
            (t["x"],t["y"]) not in sites and not t["building_id"] and t["zone"]!=kind}
        if not eligible:return
        prices=self.s.last_state["budget"]["construction_prices"]
        cost=len(eligible)*prices["park_tile" if kind==4 else "office_tile" if kind==5 else "rci_tile"]
        await self.wait("zoning purchase reserve",funds=cost+5000)
        if not self.s.last_state["tools"]["zone"]:await self.keys("Z")
        for _ in range(6):
            if self.s.last_state["tools"]["zone_type"]==kind:break
            await self.keys("Z")
        before=self.s.last_state["funds"]
        for y in sorted({y for x,y in eligible}):
            xs=sorted(x for x,py in eligible if py==y)
            start=last=xs[0]
            for x in xs[1:]:
                if x!=last+1:
                    await self.drag((start,y),(last,y));start=x
                last=x
            await self.drag((start,y),(last,y))
        assert before-self.s.last_state["funds"]==cost
        self.decision("zone",before,kind=kind,start=a,end=b,tiles=len(eligible))

    async def service(self, kind, x, y, source="Generic"):
        clinic = kind == 2 and self.args.neighborhood_clinics and (x,y) != (48,51)
        if kind==6:
            if (x,y)==(20,59): y=61
            if (x,y)==(26,55): y=57
            if (x,y)==(65,39): y=41
        basic = {0:5000, 1:4500, 2:6500, 3:5500, 4:12000, 5:8000, 6:7000, 7:9000}
        prices = self.s.last_state["budget"]["construction_prices"]
        cost = basic.get(kind, prices.get({8:"recycling", 9:"cemetery", 10:"crematorium", 11:"incinerator"}.get(kind), 0))
        if clinic: cost = 2500
        if kind == 4 and source == "Wind":
            cost = 6000
        await self.wait("purchase service", funds=cost + 5000)
        if not self.s.last_state["tools"]["service"]:
            await self.keys("S")
        for _ in range(12):
            if self.s.last_state["tools"]["service_type"] == kind:
                break
            await self.keys("S")
        if kind == 4:
            for _ in range(8):
                if self.s.last_state["tools"]["power_source"] == source:
                    break
                await self.keys("V")
        if kind == 2 and self.s.last_state["tools"].get("clinic", False) != clinic:
            await self.keys("V")
        if kind == 5 and self.s.last_state["tools"]["surface_water"]:
            await self.keys("V")
        x,y=self.streets.site((x,y))
        x0,y0=max(0,x-3),max(0,y-3)
        cells=(await self.s.request("inspect",{"section":"tiles","x":x0,"y":y0,
            "width":7,"height":7,"limit":512}))["items"]
        roads={tuple(p) for road in self.streets.records for p in road["path"]}
        occupied={(f["x"],f["y"]) for f in (await self.s.request("inspect",{"section":"facilities","limit":512}))["items"]}
        candidates=[(t["x"],t["y"]) for t in cells if not t["road"] and not t["building_id"] and
            t["type"]!=2 and not t["regional_connection_id"] and (t["x"],t["y"]) not in occupied and
            any((t["x"]+dx,t["y"]+dy) in roads for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))]
        assert candidates,("No service frontage",kind,x,y)
        x,y=min(candidates,key=lambda p:(abs(p[0]-x)+abs(p[1]-y),p))
        before, count = self.s.last_state["funds"], self.s.last_state["facilities"]
        p = await self.at((x, y))
        await self.click(p["x"], p["y"])
        assert self.s.last_state["facilities"] == count + 1, (kind, x, y, self.s.last_state["toast"])
        assert before - self.s.last_state["funds"] == cost
        self.decision("service", before, kind=kind, position=(x,y), source=source)

    async def taxes(self, rates):
        await self.keys("F6")
        await self.control("budget_taxes")
        before = self.s.last_state["funds"]
        for i, target in enumerate(rates):
            while self.s.last_state["budget"]["tax_percent"][i] != target:
                sign = "plus" if self.s.last_state["budget"]["tax_percent"][i] < target else "minus"
                await self.control(f"tax_{i}_{sign}")
        await self.keys("Escape")
        self.decision("taxes", before, rates=rates)

    async def unique(self, kind):
        item = self.s.last_state["budget"]["unique_buildings"][kind-1]
        await self.wait("unique achievement and funds", funds=item["cost"] + 25000)
        assert self.s.last_state["budget"]["unique_buildings"][kind-1]["unlocked"], item
        await self.keys("F5")
        session = json.loads(self.s.artifact("urban_sim_session.json").read_text())
        core = json.loads(self.s.artifact("urban_sim_session.json.city.json").read_text())
        tiles = {(t["x"],t["y"]):t for t in core["tiles"]}
        sites = {(f["x"],f["y"]) for f in session["facilities"]}
        candidates = [p for p,t in tiles.items() if not t["zone"] and not t["hasRoad"] and not t["buildingId"] and
            t["type"] != 2 and t["connectedToPower"] and t["connectedToWater"] and p not in sites and
            any(tiles.get((p[0]+dx,p[1]+dy),{}).get("hasRoad",False) for dx,dy in ((1,0),(-1,0),(0,1),(0,-1))) and
            8 <= p[0] <= 64 and 40 <= p[1] <= 61]
        assert candidates
        position = min(candidates, key=lambda p:(abs(p[0]-40)+abs(p[1]-48),p))
        selected = self.s.last_state["tools"]["unique_kind"]
        await self.keys("E", *(["Down"] * ((kind-selected)%6)), "Enter")
        before = self.s.last_state["funds"]
        p = await self.at(position)
        await self.click(p["x"],p["y"])
        assert self.s.last_state["budget"]["unique_buildings"][kind-1]["built"], self.s.last_state["toast"]
        assert before - self.s.last_state["funds"] == item["cost"]
        self.decision("unique", before, key=item["key"], position=position)

    async def continuation(self, checkpoint):
        def contents(path):
            session = json.loads(path.read_text())
            core = json.loads(Path(session["citySnapshot"]).read_text())
            positions = {b["id"]: b["position"] for b in core["buildings"]}
            for b in core["buildings"]:
                b.pop("id")
            for group in core["populationGroups"]:
                group.pop("id")
            for tile in core["tiles"]:
                tile["buildingId"] = positions.get(tile["buildingId"])
            for key in ("buildings", "populationGroups", "roads", "tiles"):
                core[key].sort(key=lambda value: json.dumps(value, sort_keys=True))
            fields = ("funds", "tick", "treasuryDebt", "treasuryInterestRemainder",
                      "populationChange", "migrationConditions", "disasters")
            return {k: session[k] for k in fields}, core

        self.stage = "continuation verification"
        await self.step(100)
        gui = await self.checkpoint("continuation-gui")
        expected = contents(gui)
        # F9 loads the native save written by checkpoint(), so restore the base files.
        base = json.loads(checkpoint.read_text())
        shutil.copyfile(base["citySnapshot"], self.s.artifact("urban_sim_session.json.city.json"))
        base["citySnapshot"] = str(self.s.artifact("urban_sim_session.json.city.json"))
        self.s.artifact("urban_sim_session.json").write_text(json.dumps(base))
        await self.keys("F9")
        await self.step(100)
        replayed = await self.checkpoint("continuation-reloaded")
        assert contents(replayed) == expected, "native save/load continuation diverged"
        cli = Path(self.args.executable).resolve().with_name("UrbanSimCore-cli")
        output = self.s.artifact("continuation-cli.json")
        process = await asyncio.create_subprocess_exec(str(cli), "--simulate", "100",
            "--simulate-session", str(checkpoint), "--simulate-session-out", str(output),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await process.communicate()
        self.s.artifact("continuation-cli.log").write_bytes(stdout + stderr)
        assert process.returncode == 0, stderr.decode()
        assert contents(output) == expected, "CLI continuation diverged"
        self.s.artifact("continuation.json").write_text(json.dumps({
            "ticks": 100, "native_reload_exact": True, "cli_exact": True}, indent=2))

    async def run(self):
        resume = (self.args.resume_after_utilities or self.args.resume_after_tier2500
                  or self.args.resume_before_final_growth)
        terrain = not self.args.flat_map
        if resume:
            source = Path(resume).resolve()
            prior = json.loads((source.parent / "build-identity.json").read_text())
            terrain = prior["terrain"]
        terrain_profile = "journey" if terrain else None
        await self.s.launch(terrain_profile=terrain_profile)
        self.s.artifact("build-identity.json").write_text(json.dumps({
            "executable":str(Path(self.args.executable).resolve()),
            "executable_sha256":hashlib.sha256(Path(self.args.executable).read_bytes()).hexdigest(),
            "runner_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "terrain_seed":42,"simulation_seed":98765,"map_size":96,
            "terrain":terrain,"terrain_profile":terrain_profile},indent=2))
        await record(self.s, "start")
        try:
            if resume:
                assert prior["executable_sha256"]==hashlib.sha256(Path(self.args.executable).read_bytes()).hexdigest()
                self.s.import_save(source)
                await self.keys("L")
                if not self.s.last_state["paused"]: await self.keys("Space")
                geometry=source.with_suffix(".roads.json")
                if geometry.exists():
                    profile=json.loads(geometry.read_text())
                    assert profile["profile"]==self.args.road_profile
                    self.streets=StreetLayout(profile["profile"],profile["streets"])
                else: assert self.args.road_profile=="grid", "Legacy checkpoints require --road-profile grid"
                tick=self.s.last_state["tick"]
                self.rows={}
                with (source.parent/"money.csv").open() as stream:
                    for row in csv.DictReader(stream):
                        row={k:int(v) for k,v in row.items()}
                        if row["tick"]<=tick:self.rows[row["tick"]]=row
                self.samples=[v for v in json.loads((source.parent/"measurements.json").read_text()) if v["tick"]<=tick]
                self.decisions=[v for v in json.loads((source.parent/"decisions.json").read_text()) if v["tick"]<=tick]
                self.s.artifact("resume.json").write_text(json.dumps({"source":str(source),"tick":tick,"funds":self.s.last_state["funds"],"executable_sha256":prior["executable_sha256"]},indent=2))
                if self.args.calibration:
                    await self.keys("F3","F4",*(["Minus"]*14),*(["Down"]*20))
                else: await self.player.moderate_zoom(24)
            else:
                await self.control("size_96")
                if (await self.s.request("inspect", {"section":"ui"}))["terrain"] != terrain:
                    await self.control("terrain")
                await self.control("new")
                if not self.s.last_state["paused"]:
                    await self.keys("Space")
                if self.args.calibration:
                    await self.keys("F3", "F4", *(["Minus"]*14), *(["Down"]*20))
                else:
                    await self.player.moderate_zoom(24)
                await self.road((32,48),(64,48))
                await self.service(4,47,47)
                await self.service(5,49,47)
                await self.pipes((48,48),(48,48),kind=4)
                for y in (47,49):
                    for kind,a,b in ((1,32,46),(2,50,56),(3,58,64)):
                        await self.zone(kind,(a,y),(b,y))
                await self.taxes((10,12,12,12,12))
                await self.step()
                await self.service(3,46,49)
                await self.service(2,48,49)
                await self.wait("serviced opening",population=300)
                await self.checkpoint("opening")
                if self.args.opening_only:
                    self.s.artifact("opening-result.json").write_text(json.dumps({"opening_verified":True,"final":self.samples[-1]},indent=2))
                    return

                self.stage = "local expansion before 500"
                await self.service(0,47,49)
                await self.service(1,57,47)
                await self.road((32,44),(64,44))
                await self.road((48,44),(48,48))
                for y in (43,45):
                    for kind,a,b in ((1,32,46),(2,50,56),(3,58,64)):
                        await self.zone(kind,(a,y),(b,y))
                await self.service(6,47,43)
                await self.service(7,49,43)
                self.stage="sewage failure and recovery"
                await self.pipes((48,48),(48,48),kind=4,remove=True)
                await self.step(10)
                await self.pipes((48,48),(48,48),kind=4)
                await self.step(100)
                assert self.s.last_state["environment"]["sewageBacklog"]<1e-5,self.s.last_state["environment"]
                await self.checkpoint("utility-recovery")

            if not (self.args.resume_after_tier2500 or self.args.resume_before_final_growth):
                self.stage = "500: arterial and first waste/deathcare upgrades"
                await self.wait("500 tier and purchase reserve", population=500, funds=55000)
                await self.road((32,48),(64,48),True)
                await self.service(8,49,45)
                await self.service(9,47,45)
                await self.zone(4,(32,46),(44,46))
                await self.checkpoint("tier-500")

                self.stage = "1200: first office neighborhood"
                await self.wait("1200 tier and neighborhood reserve",population=1200,funds=90000)
                await self.road((32,52),(64,52),True)
                await self.road((57,48),(57,52),True)
                for kind,x,y in ((4,47,51),(5,49,51),(3,46,51),(2,48,51),(0,47,53),(1,49,53),(6,48,53),(7,31,52)):
                    await self.service(kind,x,y,"Wind" if kind==4 else "Generic")
                for y in (51,53):
                    for kind,a,b in ((1,32,45),(5,50,56),(3,58,64)):
                        await self.zone(kind,(a,y),(b,y))
                await self.taxes((5,8,6,9,5))
                await self.checkpoint("tier-1200")

                self.stage = "2500: permanent waste and deathcare"
                await self.wait("2500 tier",population=2500)
                await self.service(11,46,53)
                await self.service(10,57,45)
                await self.checkpoint("tier-2500")

            if not self.args.resume_before_final_growth:
                self.stage = "second southern neighborhood"
                await self.wait("neighborhood reserve",funds=100000)
                for y in (56,60):
                    await self.road((32,y),(64,y),True)
                await self.road((57,52),(57,60),True)
                for kind,x,y in ((4,47,55),(5,49,55),(2,48,55),(3,46,55),(0,47,57),(1,49,57),
                                 (6,48,57),(7,31,56),(0,31,60),(1,49,59),(6,46,61)):
                    await self.service(kind,x,y,"Wind" if kind==4 else "Generic")
                for y in (55,57,59,61):
                    for kind,a,b in ((1,32,45),(5,50,56),(3,58,64)):
                        await self.zone(kind,(a,y),(b,y))

                self.stage = "serviced western neighborhood"
                await self.wait("housing expansion reserve",population=3000,funds=150000)
                for y in (44,48,52,56,60):
                    await self.road((8,y),(30,y),True)
                await self.road((30,44),(32,44),True)
                await self.road((16,44),(16,60),True)
                for kind,x,y in ((4,20,47),(5,22,47),(2,20,43),(3,22,43),(0,24,43),(1,26,43),
                                 (6,20,59),(7,22,59),(8,24,59),(9,26,59),(0,28,59),(1,28,55),(6,26,55)):
                    await self.service(kind,x,y,"Wind" if kind==4 else "Generic")
                for y in (43,45,47,49,51,53,55,57,59,61):
                    await self.zone(1,(8,y),(15,y))
                    await self.zone(1,(17,y),(19,y))
                    start=28 if y==43 else 23 if y==47 else 29 if y==59 else 29 if y==55 else 21
                    await self.zone(1,(start,y),(30,y))

                self.stage = "retail and final service capacity"
                await self.wait("retail street reserve",funds=75000)
                await self.road((64,48),(66,48),True)
                await self.road((66,40),(66,48),True)
                await self.road((32,40),(66,40),True)
                for kind,x,y in ((4,47,39),(5,49,39),(2,47,41),(3,49,41),(0,48,41),(1,48,39),(6,65,39)):
                    await self.service(kind,x,y,"Wind" if kind==4 else "Generic")
                for y in (39,41):
                    await self.zone(2,(32,y),(46,y))
                    await self.zone(2,(50,y),(64,y))
            # Keep the established household rates while rebuilding reserves.
            mix=self.streets.summary()
            if self.args.road_profile=="mixed": assert mix["balanced"] and mix["grid"]==9,mix
            await self.taxes((5,8,6,9,5))
            await self.checkpoint("city-infrastructure")
            self.stage = "grow to 10000"
            await self.wait("10000 residents",population=10000)
            first_10k = {k:self.s.last_state[k] for k in ("tick","population","funds","revenue","expenses")}
            self.s.artifact("first-ten-thousand.json").write_text(json.dumps(first_10k,indent=2))
            checkpoint = await self.checkpoint("ten-thousand")
            self.stage="sustainable operations"
            await self.step(100)
            recent=[self.rows[t] for t in sorted(self.rows)[-100:]]
            # Redevelopment is a one-off capital expense, so assess operating
            # health separately while also requiring positive total cash flow.
            assert len(recent)==100 and sum(r["revenue"]-r["expenses"] for r in recent)>0,recent[-5:]
            assert all(all(v==100 for v in r["service_percent"]) and r["debt"]==0 for r in recent)
            self.s.artifact("sustainability.json").write_text(json.dumps({
                "ticks":100,"total_revenue":sum(r["revenue"] for r in recent),
                "total_expenses_including_growth":sum(r["expenses"] for r in recent),
                "cash_gain_after_growth":sum(r["revenue"]-r["expenses"] for r in recent),
                "full_service_funding":all(all(v==100 for v in r["service_percent"]) for r in recent),
                "no_debt":all(r["debt"]==0 for r in recent)},indent=2))
            assert self.s.last_state["population"]>=10000,self.s.last_state["population"]
            checkpoint=await self.checkpoint("sustainable-ten-thousand")
            await self.continuation(checkpoint)
            if self.args.calibration: await self.keys("F4")
            await self.s.screenshot("balanced-ten-thousand")
            self.s.artifact("result.json").write_text(json.dumps({"complete":True,"first_10k":first_10k,"reserve_pass":200000<=first_10k["funds"]<=500000,"calibration":self.args.calibration,"road_mix":{k:v for k,v in self.streets.summary().items() if k!="streets"},"roadside_pipes_continuous":True,"street_network_connected":True,"final":self.samples[-1]},indent=2))
        finally:
            if not self.s.broken:
                if self.s.last_state.get("phase")!="start_screen":
                    await self.keys("F5")
                await record(self.s,"stop","balanced-native-journey")
            print("RUN",self.s.run_dir,flush=True)
            await self.s.stop()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",default="build-strict/bin/UrbanSimCore-playtest")
    parser.add_argument("--artifacts",default="playtest-artifacts/balanced-journey")
    parser.add_argument("--max-ticks",type=int,default=40000)
    parser.add_argument("--road-profile",choices=("mixed","grid"),default="mixed",help="Half grid / half freeform curved new streets by default")
    parser.add_argument("--calibration",action="store_true",help="Fast native input for calibration; acceptance uses paced 3D gestures")
    parser.add_argument("--resume-after-utilities",help="Retained utility-recovery checkpoint from this exact executable")
    parser.add_argument("--resume-after-tier2500",help="Retained tier-2500 checkpoint from this exact executable")
    parser.add_argument("--resume-before-final-growth",help="Retained city-infrastructure checkpoint from this exact executable")
    parser.add_argument("--opening-only",action="store_true")
    parser.add_argument("--flat-map",action="store_true",help="Use the historical flat map for a fresh run")
    parser.add_argument("--neighborhood-clinics",action="store_true",help="Four local clinics and one central hospital")
    asyncio.run(Journey(parser.parse_args()).run())
