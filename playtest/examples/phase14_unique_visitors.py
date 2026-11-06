"""Calibrate the optional M52 visitor unique using normal journey funds.

Load the roughly 5,000-resident checkpoint, buy a road-accessible park, enable
tourism, and wait for 10,000 admitted visitors. Never grant achievement progress.
"""
import argparse
import asyncio
import json
import statistics
from urban_sim_mcp.session import Session


async def main(args):
    s=Session(args.executable,args.artifacts,timeout=120)
    await s.launch()
    try:
        await s.city("load",import_path=args.source)
        assert s.last_state["population"]>=5000
        assert not s.last_state["budget"]["unique_buildings"][3]["unlocked"]
        await s.request("tool",{"action":"apply","name":"zone","zone":"park","x1":0,"y1":47,"x2":0,"y2":47})
        await s.request("budget",{"action":"set_tourism","tourism_enabled":True})
        start=s.last_state["tick"]
        for _ in range(2000):
            item=s.last_state["budget"]["unique_buildings"][3]
            if item["unlocked"]:
                break
            await s.request("time",{"action":"step","ticks":100 if item["progress"]<8000 else 1})
        else:
            raise AssertionError("Visitor achievement did not unlock")
        earned=s.last_state["tick"]
        await s.request("time",{"action":"step","ticks":50})
        samples=sorted(s.last_state["budget"]["history"],key=lambda row:abs(row["tick"]-earned))[:100]
        surplus=statistics.median(row["revenue"]-row["expenses"] for row in samples)
        item=s.last_state["budget"]["unique_buildings"][3]
        reference_ticks=item["cost"]/surplus
        assert 300<=reference_ticks<=600,(surplus,reference_ticks)
        await s.request("window",{"action":"resize","width":800,"height":640})
        await s.request("budget",{"action":"uniques"})
        await s.screenshot("m52-visitor-achievement-minimum")
        await s.city("save")
        report={"source":args.source,"start_tick":start,"earned_tick":earned,
                "progress":item["progress"],"cost":item["cost"],"median_surplus":surplus,
                "surplus_ticks":reference_ticks,"debt":s.last_state["debt"]}
        s.artifact("visitor-report.json").write_text(json.dumps(report,indent=2))
        assert report["debt"]==0
        print(json.dumps({"directory":str(s.run_dir),"report":report},indent=2))
    finally:
        await s.stop()


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable",required=True)
    parser.add_argument("--source",required=True)
    parser.add_argument("--artifacts",default="playtest-artifacts/m52/visitors")
    asyncio.run(main(parser.parse_args()))
