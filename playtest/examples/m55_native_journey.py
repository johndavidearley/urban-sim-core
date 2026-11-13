"""M55 fresh-city journey with mandatory water/sewer networks: native purchases, normal funds and catalog gates.

This is a fixed playthrough, not a budget-playing agent. Named stages wait for
their milestone and purchase reserve. All construction, policy and time changes
use native input. The original phase10_native_journey.py remains unchanged.
"""
import argparse
import asyncio
import csv
import json
import shutil
import hashlib
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record


class Journey:
    def __init__(self, args):
        self.args = args
        self.s = Session(args.executable, args.artifacts, timeout=120)
        self.rows = {}
        self.samples = []
        self.decisions = []
        self.stage = "opening"

    async def keys(self, *names):
        await self.s.request("input", {"events": [{"type": "key", "key": key} for key in names]})

    async def click(self, x, y):
        await self.s.request("input", {"events": [
            {"type": "move", "x": x, "y": y},
            {"type": "down", "button": "left", "x": x, "y": y},
            {"type": "up", "button": "left", "x": x, "y": y}]})

    async def control(self, name):
        controls = (await self.s.request("inspect", {"section": "ui"}))["controls"]
        c = next(c for c in controls if c["name"] == name)
        await self.click(c["x"] + c["width"] // 2, c["y"] + c["height"] // 2)

    async def at(self, p):
        point = await self.s.request("coordinates", {"direction": "tile_to_screen", "x": p[0], "y": p[1]})
        assert 0 <= point["x"] < 1280 and 0 <= point["y"] < 740, (p, point)
        return {"x": point["x"], "y": point["y"]}

    async def drag(self, a, b):
        p, q = await self.at(a), await self.at(b)
        await self.s.request("input", {"events": [
            {"type": "move", **p}, {"type": "down", "button": "left", **p},
            {"type": "move", **q}, {"type": "up", "button": "left", **q}]})

    def decision(self, action, before, **detail):
        v = self.s.last_state
        self.decisions.append({"tick": v["tick"], "population": v["population"], "stage": self.stage,
            "action": action, "funds_before": before, "funds_after": v["funds"], **detail})
        self.s.artifact("decisions.json").write_text(json.dumps(self.decisions, indent=2))

    async def checkpoint(self, name):
        await self.keys("F5")
        core = self.s.artifact(name + ".city.json")
        shutil.copyfile(self.s.artifact("urban_sim_session.json.city.json"), core)
        session = json.loads(self.s.artifact("urban_sim_session.json").read_text())
        session["citySnapshot"] = str(core)
        path = self.s.artifact(name + ".json")
        path.write_text(json.dumps(session, indent=2))
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
            await self.step()

    async def road(self, a, b, arterial=False):
        cost = (abs(a[0]-b[0]) + abs(a[1]-b[1]) + 1) * (1500 if arterial else 100)
        await self.wait("street purchase reserve", funds=cost + 5000)
        if not self.s.last_state["tools"]["road"]:
            await self.keys("R")
        if (self.s.last_state["tools"]["road_class"] == "arterial") != arterial:
            await self.keys("A")
        before = self.s.last_state["funds"]
        await self.drag(a, b)
        assert self.s.last_state["funds"] < before, (a, b, self.s.last_state["toast"])
        self.decision("road", before, start=a, end=b, arterial=arterial)
        await self.pipes(a,b)

    async def pipes(self,a,b,kind=3,remove=False):
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
        cost = (abs(a[0]-b[0]) + 1) * (abs(a[1]-b[1]) + 1) * (1500 if kind in (4,5) else 25)
        await self.wait("zoning purchase reserve", funds=cost + 5000)
        if not self.s.last_state["tools"]["zone"]:
            await self.keys("Z")
        for _ in range(6):
            if self.s.last_state["tools"]["zone_type"] == kind:
                break
            await self.keys("Z")
        before = self.s.last_state["funds"]
        if a[1]==b[1]:
            sites={(f["x"],f["y"]) for f in (await self.s.request("inspect",{"section":"facilities","limit":512}))["items"]}
            first=None
            for x in range(a[0],b[0]+2):
                eligible=x<=b[0] and (x,a[1]) not in sites
                if eligible and first is None: first=x
                if not eligible and first is not None:
                    await self.drag((first,a[1]),(x-1,a[1]));first=None
        else: await self.drag(a,b)
        assert self.s.last_state["funds"] < before, (kind, a, b, self.s.last_state["toast"])
        self.decision("zone", before, kind=kind, start=a, end=b)

    async def service(self, kind, x, y, source="Generic"):
        if kind==6:
            if (x,y)==(20,59): y=61
            if (x,y)==(26,55): y=57
            if (x,y)==(65,39): y=41
        basic = {0:5000, 1:4500, 2:6500, 3:5500, 4:12000, 5:8000, 6:7000, 7:9000}
        prices = self.s.last_state["budget"]["construction_prices"]
        cost = basic.get(kind, prices.get({8:"recycling", 9:"cemetery", 10:"crematorium", 11:"incinerator"}.get(kind), 0))
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
        await self.s.launch()
        self.s.artifact("build-identity.json").write_text(json.dumps({
            "executable":str(Path(self.args.executable).resolve()),
            "executable_sha256":hashlib.sha256(Path(self.args.executable).read_bytes()).hexdigest(),
            "runner_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "terrain_seed":42,"simulation_seed":98765,"map_size":96,"terrain":False},indent=2))
        await record(self.s, "start")
        try:
            await self.control("size_96")
            if (await self.s.request("inspect", {"section":"ui"}))["terrain"]:
                await self.control("terrain")
            await self.control("new")
            if not self.s.last_state["paused"]:
                await self.keys("Space")
            await self.keys("F3", "F4", *(["Minus"]*14), *(["Down"]*20))
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
            await self.step(2300)
            await self.checkpoint("opening-2400")
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

            self.stage = "500: arterial and first waste/deathcare upgrades"
            await self.wait("500 tier and purchase reserve", population=500, funds=300000)
            await self.road((32,48),(64,48),True)
            await self.service(8,49,45)
            await self.service(9,47,45)
            await self.zone(4,(32,46),(44,46))
            await self.checkpoint("tier-500")

            self.stage = "1200: first office neighborhood"
            await self.wait("1200 tier and neighborhood reserve",population=1200,funds=140000)
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

            self.stage = "second southern neighborhood"
            await self.wait("neighborhood reserve",funds=180000)
            for y in (56,60):
                await self.road((32,y),(64,y),True)
            await self.road((57,52),(57,60),True)
            for kind,x,y in ((4,47,55),(5,49,55),(2,48,55),(3,46,55),(0,47,57),(1,49,57),
                             (6,48,57),(7,31,56),(0,31,60),(1,49,59),(6,46,61)):
                await self.service(kind,x,y,"Wind" if kind==4 else "Generic")
            for y in (55,57,59,61):
                for kind,a,b in ((1,32,45),(5,50,56),(3,58,64)):
                    await self.zone(kind,(a,y),(b,y))

            self.stage = "5000: serviced western neighborhood"
            await self.wait("5000 tier and neighborhood reserve",population=5000,funds=300000)
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
            await self.wait("retail street reserve",funds=200000)
            await self.road((64,48),(66,48),True)
            await self.road((66,40),(66,48),True)
            await self.road((32,40),(66,40),True)
            for kind,x,y in ((4,47,39),(5,49,39),(2,47,41),(3,49,41),(0,48,41),(1,48,39),(6,65,39)):
                await self.service(kind,x,y,"Wind" if kind==4 else "Generic")
            for y in (39,41):
                await self.zone(2,(32,y),(46,y))
                await self.zone(2,(50,y),(64,y))
            # The serviced 5k expansion permits a fixed one-point household tax cut.
            await self.taxes((4,8,6,9,4))
            await self.checkpoint("city-infrastructure")
            self.stage = "grow to 10000"
            await self.wait("10000 residents",population=10000)
            checkpoint = await self.checkpoint("ten-thousand")
            self.stage="sustainable operations"
            await self.step(100)
            recent=[self.rows[t] for t in sorted(self.rows)[-100:]]
            assert len(recent)==100 and all(r["revenue"]>=r["expenses"] for r in recent),recent[-5:]
            assert self.s.last_state["population"]>=10000,self.s.last_state["population"]
            checkpoint=await self.checkpoint("sustainable-ten-thousand")
            await self.continuation(checkpoint)
            await self.keys("F4")
            await self.s.screenshot("m55-ten-thousand")
            self.s.artifact("result.json").write_text(json.dumps({"complete":True,"final":self.samples[-1]},indent=2))
        finally:
            if not self.s.broken:
                if self.s.last_state.get("phase")!="start_screen":
                    await self.keys("F5")
                await record(self.s,"stop","m55-native-journey")
            print("RUN",self.s.run_dir,flush=True)
            await self.s.stop()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",default="build-strict/bin/UrbanSimCore-playtest")
    parser.add_argument("--artifacts",default="playtest-artifacts/m55-journey")
    parser.add_argument("--max-ticks",type=int,default=40000)
    parser.add_argument("--opening-only",action="store_true")
    asyncio.run(Journey(parser.parse_args()).run())
