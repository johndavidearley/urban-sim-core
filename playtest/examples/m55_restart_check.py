"""Verify native quit/relaunch/load of an earned M55 checkpoint.

Only relocates unchanged saves through Session.import_save; no funds, population,
progression, topology or simulation fields are edited. Load uses the start-screen
L shortcut. Both resumed cities advance with native N input.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record

async def key(s,name):
    await s.request("input",{"events":[{"type":"key","key":name}]})

async def check(args):
    checkpoint=Path(args.session).resolve()
    saved=json.loads(checkpoint.read_text())
    first=Session(args.executable,args.artifacts,timeout=120)
    second=Session(args.executable,args.artifacts,timeout=120)
    result={"checkpoint":str(checkpoint),"checkpoint_sha256":hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
            "executable_sha256":hashlib.sha256(Path(args.executable).read_bytes()).hexdigest()}
    try:
        await first.launch();await record(first,"start")
        first.import_save(str(checkpoint));await key(first,"L")
        assert first.last_state["tick"]==saved["tick"]
        assert first.last_state["funds"]==saved["funds"]
        await key(first,"N");assert first.last_state["tick"]==saved["tick"]+1
        await first.request("window",{"action":"close"})
        c=next(c for c in (await first.request("inspect",{"section":"ui"}))["controls"] if c["name"]=="save_and_quit")
        x,y=c["x"]+c["width"]//2,c["y"]+c["height"]//2
        await first.request("input",{"events":[{"type":"down","button":"left","x":x,"y":y},{"type":"up","button":"left","x":x,"y":y}]})
        if first.process.stdin: first.process.stdin.close()
        code=await asyncio.wait_for(first.process.wait(),10)
        assert code==0,code
        await record(first,"stop","m55-native-save-and-quit")
        resume=first.artifact("urban_sim_session.json")
        result.update(first_run=str(first.run_dir),quit_exit_code=code)
        await second.launch();await record(second,"start")
        second.import_save(str(resume));await key(second,"L")
        after_quit=json.loads(resume.read_text())
        assert second.last_state["tick"]==after_quit["tick"]==saved["tick"]+1
        assert second.last_state["funds"]==after_quit["funds"]
        await key(second,"N");assert second.last_state["tick"]==saved["tick"]+2
        await second.screenshot("m55-city-resumed-after-quit")
        await record(second,"stop","m55-native-relaunch-load")
        result.update(complete=True,resumed_tick=second.last_state["tick"],population=second.last_state["population"],second_run=str(second.run_dir))
        first.artifact("restart-result.json").write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2),flush=True)
    finally:
        await first.stop();await second.stop()

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable",default="build-strict/bin/UrbanSimCore-playtest")
    p.add_argument("--artifacts",default="playtest-artifacts/m55-restart")
    p.add_argument("--session",required=True)
    asyncio.run(check(p.parse_args()))
