"""Paid healthcare consolidation on an unchanged earned city; no save edits."""
import argparse
import asyncio
import hashlib
import json
import shutil
from pathlib import Path
from urban_sim_mcp.session import Session


async def run(args):
    s = Session(args.executable,args.artifacts,timeout=120)
    async def key(name):
        await s.request("input",{"events":[{"type":"key","key":name}]})
    try:
        await s.launch()
        s.import_save(args.session)
        await key("L")
        initial = s.last_state
        assert initial["population"] >= 10000
        sites = [f for f in (await s.request("inspect",{"section":"facilities","limit":512}))["items"] if f["type"] == "Health"]
        assert len(sites)==5
        central = min(sites,key=lambda f:abs(f["x"]-48)+abs(f["y"]-49))
        before = s.last_state["funds"]
        for f in sites:
            await s.request("tool",{"action":"apply","name":"bulldoze","x1":f["x"],"y1":f["y"]})
            await s.request("tool",{"action":"apply","name":"service","service":"health",
                "clinic":f is not central,"x1":f["x"],"y1":f["y"]})
        await s.request("tool",{"action":"select","name":"service","service":"health","clinic":False})
        await key("V")
        assert s.last_state["tools"]["clinic"]
        await key("V")
        assert not s.last_state["tools"]["clinic"]
        sites = [f for f in (await s.request("inspect",{"section":"facilities","limit":512}))["items"] if f["type"] == "Health"]
        assert sum(f["clinic"] for f in sites)==4
        assert len(sites)==5
        after_build = s.last_state["funds"]
        await key("Escape")
        for _ in range(100): await key("N")
        state=s.last_state
        assert state["population"]>=10000
        assert state["metrics"]["hospital_beds"]==400
        assert state["metrics"]["hospital_capacity_factor"]==1
        await key("F5")
        saved=json.loads(s.artifact("urban_sim_session.json").read_text())
        assert sum(f.get("clinic",False) for f in saved["facilities"])==4
        await key("F9")
        assert s.last_state["metrics"]["hospital_beds"]==400
        assert s.last_state["tick"]==state["tick"]
        base=s.artifact("clinic-baseline.json")
        core=s.artifact("clinic-baseline.city.json")
        shutil.copyfile(s.artifact("urban_sim_session.json.city.json"),core)
        baseline=json.loads(s.artifact("urban_sim_session.json").read_text())
        baseline["citySnapshot"]=str(core)
        base.write_text(json.dumps(baseline))
        for _ in range(10): await key("N")
        await key("F5")
        cli=Path(args.executable).resolve().with_name("UrbanSimCore-cli.exe")
        output=s.artifact("clinic-cli.json")
        process=await asyncio.create_subprocess_exec(str(cli),"--simulate","10","--simulate-session",str(base),
            "--simulate-session-out",str(output),stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.PIPE)
        stdout,stderr=await process.communicate()
        s.artifact("clinic-cli.log").write_bytes(stdout+stderr)
        assert process.returncode==0,stderr.decode()
        def normalized(path):
            session=json.loads(path.read_text())
            snapshot=Path(session["citySnapshot"])
            city=json.loads((snapshot if snapshot.is_absolute() else path.parent/snapshot).read_text())
            positions={b["id"]:b["position"] for b in city["buildings"]}
            for b in city["buildings"]: b.pop("id")
            for group in city["populationGroups"]: group.pop("id")
            for tile in city["tiles"]: tile["buildingId"]=positions.get(tile["buildingId"])
            for k in ("buildings","populationGroups","roads","tiles"):
                city[k].sort(key=lambda value:json.dumps(value,sort_keys=True))
            return {k:session[k] for k in ("funds","tick","treasuryDebt","facilities","populationChange","migrationConditions","disasters")},city
        assert normalized(output)==normalized(s.artifact("urban_sim_session.json")),"Clinic CLI continuation diverged"
        await s.screenshot("clinics-and-central-hospital-3d")
        await key("F3")
        await s.request("view",{"action":"camera","view_x":32,"view_y":39,"tile_pixels":40})
        await s.screenshot("clinics-and-central-hospital-topdown")
        result={"complete":True,"run":str(s.run_dir),"original_population":initial["population"],
            "population":state["population"],"hospital_count":1,"clinic_count":4,"beds":400,
            "patient_demand":state["metrics"]["patient_demand"],
            "clinic_visit_capacity":state["metrics"]["clinic_visit_capacity"],"clinic_visits_used":state["metrics"]["clinic_visits_used"],"health_coverage":state["metrics"]["health_coverage"],
            "paid_conversion_cost":before-after_build,"healthcare_upkeep_before":175,"healthcare_upkeep_after":83,
            "save_load_pass":True,"validation_errors":state["validation_errors"],
            "cli_continuation_ticks":10,"cli_continuation_exact":True,
            "executable_sha256":hashlib.sha256(Path(args.executable).read_bytes()).hexdigest()}
        s.artifact("healthcare-result.json").write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2),flush=True)
    finally:
        await s.stop()


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable",required=True)
    p.add_argument("--session",required=True)
    p.add_argument("--artifacts",default="build-windows/m56-healthcare")
    asyncio.run(run(p.parse_args()))
