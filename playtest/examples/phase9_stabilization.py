"""M35 mature legacy compatibility, mixed-access endurance and recovery evidence.

Semantic setup is explicit; native management interaction is covered separately
by test_regional_* rendered acceptance. Never overwrites the input save.
"""
import argparse
import asyncio
import json
import time
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record, replay


def canonical(path):
    data=json.loads(path.read_text())
    snapshot=Path(data.pop("citySnapshot"))
    if not snapshot.is_absolute(): snapshot=path.parent/snapshot
    city=json.loads(snapshot.read_text())
    for key in ("buildings","populationGroups"):
        city[key].sort(key=lambda item:item["id"])
    return {"session":data,"city":city}


def checkpoint(s,name):
    path=s.artifact(name+".json")
    data=json.loads(s.artifact("urban_sim_session.json").read_text())
    core=s.artifact(name+".city.json")
    core.write_bytes(s.artifact("urban_sim_session.json.city.json").read_bytes())
    data["citySnapshot"]=str(core);path.write_text(json.dumps(data))
    return path


async def run(args):
    root=Path(args.artifacts).resolve();root.mkdir(parents=True,exist_ok=True)
    s=Session(args.executable,root,timeout=120)
    samples=[]
    try:
        await s.launch();await record(s,"start")
        await s.city("load",import_path=args.legacy_save)
        assert not s.last_state["metrics"]["tourism_enabled"]
        await s.request("time",{"action":"step","ticks":20})
        legacy=(await s.request("inspect",{"section":"tourism"}))["tourism"]
        assert legacy["report"]["staying"]==legacy["report"]["dayArrivals"]==0
        await s.city("save")
        legacy_path=checkpoint(s,"legacy96-baseline")
        data=json.loads(legacy_path.read_text());core=json.loads(Path(data["citySnapshot"]).read_text())
        assert core["map"]=={"width":96,"height":96},"Use the retained 96x96 mature fixture"
        # An unused edge-connected coast outside the legacy developed footprint.
        for tile in core["tiles"]:
            if tile["y"]==28 and tile["x"]<=29:
                assert not tile.get("buildingId",0) and not tile.get("hasRoad",False)
                tile["type"]=2
        coast=s.artifact("mature-coast.city.json");coast.write_text(json.dumps(core))
        data["citySnapshot"]=str(coast)
        adapted=s.artifact("mature-coast.json");adapted.write_text(json.dumps(data))
        await s.city("load",import_path=str(adapted))
        for x1,y1,x2,y2 in ((0,30,35,30),(0,33,26,33),(17,30,17,33),(0,30,0,48),(30,30,30,36)):
            await s.request("tool",{"action":"apply","name":"road","x1":x1,"y1":y1,"x2":x2,"y2":y2})
        for service,x in (("power",15),("water",17)):
            await s.request("tool",{"action":"apply","name":"service","service":service,"x1":x,"y1":29})
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":2,"y1":31,"x2":11,"y2":31})
        for name,x,y,extra in (("port",22,29,{"port_action":"cargo"}),
                ("port",24,29,{"port_action":"passenger"}),
                ("airport",18,31,{}),("hotel",8,29,{})):
            await s.request("tool",{"action":"apply","name":name,"x1":x,"y1":y,**extra})
        await s.request("time",{"action":"step","ticks":3})
        await s.city("save")
        mixed_path=checkpoint(s,"mature96-mixed-baseline")

        async def sample(phase,ticks=100):
            previous=s.last_state["tick"];cash=s.last_state["funds"]
            started=time.perf_counter()
            await s.request("time",{"action":"step","ticks":ticks})
            elapsed=time.perf_counter()-started
            state=s.last_state
            assert state["tick"]==previous+ticks
            history=[row for row in state["budget"]["history"] if row["tick"]>previous]
            assert len(history)==ticks
            # This retained, solvent fixture has no debt; all tick cash changes
            # must reconcile to the complete bounded history window.
            assert state["debt"]==0
            assert state["funds"]-cash==sum(row["revenue"]-row["expenses"] for row in history)
            visitors=(await s.request("inspect",{"section":"tourism"}))["tourism"]
            cargo=await s.request("inspect",{"section":"regional_report"})
            alerts=(await s.request("inspect",{"section":"regional_alerts"}))["items"]
            r=visitors["report"]
            assert r["dayArrivals"]+r["unmetDayTransport"]==r["requestedDay"]
            assert r["overnightArrivals"]+r["unmetOvernightTransport"]+r["unmetOvernightBeds"]==r["requestedOvernight"]
            for row in visitors["travel"]["routes"]:
                assert row["residentRiders"]+row["ridership"]<=row["capacity"]
            for direction in ("imports","exports"):
                assert cargo["allocation"]["requested"][direction]==cargo["allocation"]["admitted"][direction]+cargo["allocation"]["unmet"][direction]
                assert cargo["allocation"]["admitted"][direction]==cargo["delivered"][direction]+cargo["undelivered"][direction]
            samples.append({"phase":phase,"ticks":ticks,"step_request_seconds":elapsed,
                "state":state,"tourism":visitors,"cargo":cargo,"alerts":alerts})
            (root/"measurements.json").write_text(json.dumps(samples,indent=2))
            print(phase,state["tick"],state["population"],r["dayArrivals"],r["staying"],round(elapsed,3),flush=True)
            return visitors

        for _ in range(3): await sample("mixed")
        await s.request("tool",{"action":"apply","name":"airport","port_action":"toggle","x1":18,"y1":31})
        await sample("airport_closed")
        await s.request("tool",{"action":"apply","name":"port","port_action":"toggle","x1":24,"y1":29})
        await sample("road_only_passengers")
        capture=await s.screenshot("m35-mature-access-stress");s.artifact(capture["capture"]).unlink()
        await s.city("save");before=canonical(s.artifact("urban_sim_session.json"))
        await s.city("load");await s.city("save")
        assert canonical(s.artifact("urban_sim_session.json"))==before,"Closed-access load changed state"
        for name,x,y in (("airport",18,31),("port",24,29)):
            await s.request("tool",{"action":"apply","name":name,"port_action":"toggle","x1":x,"y1":y})
        await sample("access_recovery")
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":29})
        closed=await sample("hotel_closed")
        assert closed["report"]["staying"]==0
        await s.request("tool",{"action":"apply","name":"hotel","x1":8,"y1":29})
        for _ in range(3): await sample("hotel_recovery")
        await s.request("tool",{"action":"apply","name":"port","port_action":"remove","x1":22,"y1":29})
        assert s.last_state["metrics"]["regional_logistics_enabled"],"Demolition reverted finite cargo"
        await sample("cargo_removed")
        await s.request("tool",{"action":"apply","name":"port","port_action":"cargo","x1":22,"y1":29})
        for _ in range(2): await sample("cargo_recovery")
        capture=await s.screenshot("m35-mature-recovery");s.artifact(capture["capture"]).unlink()
        await s.city("save");saved=checkpoint(s,"final-continuation")
        await sample("continuation",20)
        await s.city("save");gui=canonical(s.artifact("urban_sim_session.json"))
        output=s.artifact("headless.json")
        cli=Path(args.executable).resolve().with_name("UrbanSimCore-cli")
        proc=await asyncio.create_subprocess_exec(str(cli),"--simulate","20","--simulate-session",str(saved),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        _,stderr=await proc.communicate();assert proc.returncode==0,stderr.decode()
        assert canonical(output)==gui,"Mature GUI/CLI continuation differs"
        scenario=await record(s,"stop","m35-mature-stress")
        result=await replay(s,scenario["scenario"],2)
        assert result["identical"],"Mature replay differs"
        summary={"legacy_checkpoint":str(legacy_path),"mixed_checkpoint":str(mixed_path),
            "samples":samples,"continuation_identical":True,"replay":result}
        (root/"results.json").write_text(json.dumps(summary,indent=2))
        print("PASS",root/"results.json",flush=True)
    finally:
        await s.stop()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",required=True)
    parser.add_argument("--legacy-save",required=True)
    parser.add_argument("--artifacts",required=True)
    asyncio.run(run(parser.parse_args()))
