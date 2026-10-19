"""M50 journey capacity recovery and paired playable/headless growth experiment."""
import argparse
import asyncio
import csv
import json
import shutil
from pathlib import Path
from urban_sim_mcp.session import Session


async def run(args):
    s = Session(args.executable, args.artifacts)
    await s.launch()
    try:
        await s.city("load", import_path=args.source)
        initial = dict(s.last_state["metrics"]["department_capacity"])
        source = json.loads(s.artifact("urban_sim_session.json").read_text())
        core = json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
        tiles = {(t["x"], t["y"]): t for t in core["tiles"]}
        sites = {(f["x"], f["y"]) for f in source["facilities"]}
        for f in source["facilities"]:
            if f.get("powerSource") == 4:
                sites.update((f["x"]+dx,f["y"]+dy) for dx in range(2) for dy in range(2))
        candidates = [p for p,t in tiles.items() if t["type"] != 2 and not t["hasRoad"] and
                      not t["buildingId"] and t["zone"] != 4 and not t.get("regionalConnectionId",0) and p not in sites and
                      any(tiles.get((p[0]+dx,p[1]+dy),{}).get("hasRoad",False) for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))]
        additions = []
        for index, name in enumerate(("fire", "police", "sanitation")):
            for _ in range(args.max_additions):
                report = s.last_state["metrics"]["department_capacity"][name]
                if report["unserved"] < 1e-6:
                    break
                rows = (await s.request("inspect", {"section":"entities", "limit":512}))["items"]
                needy = [b for b in rows if b["department_unserved"][index] > .01]
                if not needy:
                    raise RuntimeError(f"{name} has no target for {report}")
                target = max(needy, key=lambda b:(b["department_unserved"][index], -b["id"]))
                available = sorted(candidates, key=lambda p:(abs(p[0]-target["x"])+abs(p[1]-target["y"]),p))
                if not available:
                    raise RuntimeError("No empty road-connected station sites remain")
                position = available[0]
                await s.request("tool", {"action":"apply", "name":"service", "service":name,
                                          "x1":position[0], "y1":position[1]})
                candidates.remove(position)
                additions.append({"type":name, "position":position})
            else:
                raise RuntimeError(f"{name} needs more than {args.max_additions} additions")
        recovered = s.last_state["metrics"]["department_capacity"]
        await s.request("budget", {"action":"services"})
        await s.request("window", {"action":"resize", "width":800, "height":640})
        await s.screenshot("m50-recovered-services-minimum")
        await s.request("budget", {"action":"close"})
        await s.city("save")
        full = json.loads(s.artifact("urban_sim_session.json").read_text())
        snapshot = s.artifact("full.city.json")
        shutil.copyfile(s.artifact("urban_sim_session.json.city.json"),snapshot)
        full["citySnapshot"] = str(snapshot)
        full_path = s.artifact("full.json")
        full_path.write_text(json.dumps(full))
        half = json.loads(json.dumps(full))
        counts = {0:0,1:0,6:0}
        kept = []
        for f in half["facilities"]:
            if f["type"] in counts:
                counts[f["type"]] += 1
                if counts[f["type"]] % 2:
                    continue
            kept.append(f)
        half["facilities"] = kept
        half_path = s.artifact("half.json")
        half_path.write_text(json.dumps(half))
        await s.city("load", import_path=str(half_path))
        half_initial = s.last_state["metrics"]["department_capacity"]
        await s.stop()
        cli = str(Path(args.executable).with_name(Path(args.executable).name.replace("-playtest","-cli")))
        async def simulate(name,path):
            output = s.artifact(name+"-out.json")
            report = s.artifact(name+".csv")
            with s.artifact(name+"-cli.log").open("wb") as log:
                process = await asyncio.create_subprocess_exec(cli,"--simulate",str(args.ticks),
                    "--simulate-session",str(path),"--simulate-session-out",str(output),
                    "--simulate-report",str(report),stdout=log,stderr=log)
                if await process.wait():
                    raise RuntimeError(f"{name} continuation failed; see {log.name}")
            with report.open() as stream:
                rows = list(csv.DictReader(stream))
            populations = [int(r["population"]) for r in rows]
            return {"end_population":populations[-1],"mean_population":sum(populations)/len(populations),
                    "end_service_coverage":float(rows[-1]["service_coverage"]),"output":str(output)}
        measurements = await asyncio.gather(simulate("full",full_path),simulate("half",half_path))
        report = {"source":args.source,"initial_population":sum(g["size"] for g in core["populationGroups"]),
                  "ticks":args.ticks,"initial":initial,"recovered":recovered,"half_initial":half_initial,
                  "additions":additions,"full":measurements[0],"half":measurements[1]}
        report["full_growth"] = measurements[0]["end_population"] - report["initial_population"]
        report["half_growth"] = measurements[1]["end_population"] - report["initial_population"]
        report["growth_reduction"] = 1 - report["half_growth"] / max(1, report["full_growth"])
        s.artifact("capacity-report.json").write_text(json.dumps(report,indent=2))
        assert initial["fire"]["unserved"] > 0, "Journey must expose a fire shortage"
        assert all(row["unserved"] < 1e-6 for row in recovered.values()), "Recovery incomplete"
        assert report["growth_reduction"] >= .10, "Half stations should grow at least 10% slower"
        print(json.dumps({"directory":str(s.run_dir),"report":report},indent=2))
    finally:
        await s.stop()


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",required=True)
    parser.add_argument("--source",required=True)
    parser.add_argument("--artifacts",default="playtest-artifacts/m50")
    parser.add_argument("--ticks",type=int,default=2000)
    parser.add_argument("--max-additions",type=int,default=100)
    asyncio.run(run(parser.parse_args()))
