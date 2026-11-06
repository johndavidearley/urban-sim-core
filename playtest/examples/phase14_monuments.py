"""M53: native prerequisite/monument purchases in the ordinary 10k journey city.

Uses the retained M52 two-unique save, normal funds and earned unlocks. Checks
health coverage and exact native save/load plus six-tick GUI/CLI continuation.
"""
import argparse
import asyncio
import json
from pathlib import Path
from urban_sim_mcp.session import Session


async def main(args):
    s=Session(args.executable,args.artifacts,timeout=120)
    await s.launch()
    async def keys(*names):
        await s.request("input",{"events":[{"type":"key","key":key} for key in names]})
    async def checkpoint():
        await keys("F5")
        return (json.loads(s.artifact("urban_sim_session.json").read_text()),
                json.loads(s.artifact("urban_sim_session.json.city.json").read_text()))
    async def site():
        session,core=await checkpoint()
        tiles={(t["x"],t["y"]):t for t in core["tiles"]}
        sites=set()
        for f in session["facilities"]:
            span=2 if f["type"]==4 and f.get("powerSource")==4 else 1
            sites.update((f["x"]+dx,f["y"]+dy) for dx in range(span) for dy in range(span))
        candidates=[p for p,t in tiles.items() if t["type"]!=2 and not t["zone"] and not t["hasRoad"] and
            not t["buildingId"] and t["connectedToPower"] and t["connectedToWater"] and p not in sites and
            any(tiles.get((p[0]+dx,p[1]+dy),{}).get("hasRoad",False) for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))]
        assert candidates,"No empty serviced road site"
        return min(candidates,key=lambda p:(abs(p[0]-40)+abs(p[1]-48),p))
    async def place(p):
        await s.request("view",{"action":"projection","projection":"top_down"})
        await s.request("view",{"action":"camera","tile_pixels":24,"view_x":max(0,p[0]-12),"view_y":max(0,p[1]-9)})
        point=await s.request("coordinates",{"direction":"tile_to_screen","x":p[0],"y":p[1]})
        await s.request("input",{"events":[{"type":"move",**point},
            {"type":"down","button":"left",**point},{"type":"up","button":"left",**point}]})
    try:
        await s.city("load",import_path=args.source)
        assert s.last_state["population"]>=10000 and s.last_state["debt"]==0
        initial_funds=s.last_state["funds"]
        await s.request("window",{"action":"resize","width":800,"height":640})
        prerequisite_cost=0
        for index,key in enumerate(("treasury_hall","transit_exchange","trade_hall","visitor_pavilion","academy","civic_square")):
            if key not in ("treasury_hall","academy","civic_square"):
                continue
            item=s.last_state["budget"]["unique_buildings"][index]
            if item["built"]:
                continue
            assert item["unlocked"],item
            p=await site()
            distance=(index+1-s.last_state["tools"]["unique_kind"])%6
            await keys("E",*(["Down"]*distance),"Enter")
            cash=s.last_state["funds"]
            await place(p)
            assert s.last_state["budget"]["unique_buildings"][index]["built"],s.last_state["toast"]
            assert cash-s.last_state["funds"]==item["cost"]
            prerequisite_cost+=item["cost"]
        assert s.last_state["budget"]["monuments"][0]["unlocked"]
        before=dict(s.last_state["metrics"])
        p=await site()
        await keys("M")
        await s.screenshot("m53-qualified-monument-catalog-minimum")
        await keys("Enter")
        cash=s.last_state["funds"]
        await place(p)
        item=s.last_state["budget"]["monuments"][0]
        assert item["built"] and item["active"],s.last_state["toast"]
        assert cash-s.last_state["funds"]==item["cost"]
        after=dict(s.last_state["metrics"])
        assert after["health_coverage"]==1 and after["hospital_capacity_factor"]==1
        assert after["illness_rate"]<before["illness_rate"],(before,after)
        await s.request("budget",{"action":"services"})
        await s.screenshot("m53-medical-services-minimum")
        await s.request("budget",{"action":"close"})
        saved,core=await checkpoint()
        await s.request("time",{"action":"step","ticks":6})
        future={key:s.last_state[key] for key in ("funds","population","metrics","budget")}
        await s.city("load")
        again,_=await checkpoint()
        assert saved==again,"Load/save changed persisted session"
        await s.request("time",{"action":"step","ticks":6})
        assert {key:s.last_state[key] for key in future}==future,"Native continuation diverged"
        gui_session,gui_core=await checkpoint()
        core_path=s.artifact("parity-source.city.json");core_path.write_text(json.dumps(core))
        source=s.artifact("parity-source.json");source.write_text(json.dumps({**saved,"citySnapshot":str(core_path)}))
        output=s.artifact("cli-out.json")
        cli=Path(args.executable).resolve().with_name("UrbanSimCore-cli")
        with s.artifact("cli.log").open("wb") as log:
            process=await asyncio.create_subprocess_exec(str(cli),"--simulate","6","--simulate-session",str(source),
                "--simulate-session-out",str(output),stdout=log,stderr=log)
            assert await process.wait()==0,"CLI continuation failed"
        cli_session=json.loads(output.read_text());cli_core=json.loads(Path(cli_session["citySnapshot"]).read_text())
        gui_session.pop("citySnapshot");cli_session.pop("citySnapshot")
        assert gui_session==cli_session,"GUI/CLI session mismatch"
        for data in (gui_core,cli_core):
            for key in ("buildings","populationGroups"): data[key].sort(key=lambda b:b["id"])
        assert gui_core==cli_core,"GUI/CLI core mismatch"
        report={"source":args.source,"position":p,"initial_funds":initial_funds,"prerequisite_cost":prerequisite_cost,
            "monument_cost":item["cost"],"health_before":before["health_coverage"],"health_after":after["health_coverage"],
            "illness_before":before["illness_rate"],"illness_after":after["illness_rate"],
            "gui_cli_parity":True,"continuation_ticks":6,"debt":s.last_state["debt"]}
        assert report["debt"]==0
        s.artifact("monument-report.json").write_text(json.dumps(report,indent=2))
        print(json.dumps({"directory":str(s.run_dir),"report":report},indent=2))
    finally:
        await s.stop()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",required=True)
    parser.add_argument("--source",required=True)
    parser.add_argument("--artifacts",default="playtest-artifacts/m53/journey-native")
    asyncio.run(main(parser.parse_args()))
