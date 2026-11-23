"""Explore a saved city with paced native movement and local camera orbits.

This is a scripted usability walkthrough; screenshots need visual review.
It does not measure uninstrumented performance or replace outside-player testing.
"""
import argparse
import asyncio
import hashlib
import json
import math
from pathlib import Path
from urban_sim_mcp.session import Session
from urban_sim_mcp.scenarios import record
from urban_sim_mcp.paced_input import PacedInput
from urban_sim_mcp.native_pipes import lay_road_pipes


async def grid_construction(s, player, facilities):
    """Preview, cancel, then buy a short connected street using normal funds."""
    size = s.last_state["map_size"]
    width = min(22, size)
    origin = max(0, size//2-width//2)
    cells = (await s.request("inspect", {"section":"tiles", "x":origin,
        "y":origin,"width":width,"height":width,"limit":512}))["items"]
    tiles = {(t["x"],t["y"]):t for t in cells}
    sites = {(f["x"],f["y"]) for f in facilities}
    candidates = []
    for a, tile in tiles.items():
        if not tile["road"]:
            continue
        for dx, dy in ((0,1),(1,0),(0,-1),(-1,0)):
            route = [(a[0]+dx*n,a[1]+dy*n) for n in (1,2)]
            if all(p in tiles and not tiles[p]["road"] and not tiles[p]["zone"]
                   and not tiles[p]["building_id"] and tiles[p]["type"] != 2
                   and p not in sites and not tiles[p]["regional_connection_id"] for p in route):
                candidates.append((a,route[-1]))
    assert candidates, "No empty connected street site near the city centre"
    a, b = min(candidates,key=lambda pair: sum(abs(v-size//2) for v in pair[0]))
    await player.frame_tile(*a)
    await player.orbit(await player.frame_tile(*a),dx=-55,dy=8)
    # Resolve both endpoints after the camera finishes moving.
    points = []
    for tile in (a,b):
        p = await s.request("coordinates", {"direction":"tile_to_screen","x":tile[0],"y":tile[1]})
        points.append((p["x"],p["y"]))
    before = {k:s.last_state[k] for k in ("funds","road_tiles","tick")}
    await player.key("R")
    assert s.last_state["tools"]["road_class"] == "local"
    assert s.last_state["tools"]["road_layout"] == "GRID DRAG"
    quote = {}

    async def preview():
        quote.update(s.last_state["preview"]["road"])
        assert quote["valid"] and quote["new_segments"] == 2, quote
        await player.pause(2)
        await s.screenshot("street-preview")

    await player.drag(*points, on_hold=preview, cancel=True)
    assert all(s.last_state[k] == v for k,v in before.items()), "Cancelled preview mutated city"
    assert not s.last_state["tools"]["dragging"]
    await s.screenshot("street-cancelled")
    await player.pause()
    await player.key("R")
    await player.drag(*points, seconds=1.8)
    assert s.last_state["road_tiles"] == before["road_tiles"] + 2
    assert before["funds"] - s.last_state["funds"] == quote["cost"]
    await player.key("Escape")  # Leave construction before orbiting.
    await player.orbit(await player.frame_tile(*b),dx=60,dy=-8)
    await player.pause(2)
    await s.screenshot("street-built-orbit")
    return {"start":a,"end":b,"cost":quote["cost"],"new_tiles":2,"cancel_preserved_city":True}


async def curve_construction(s, player, facilities):
    """Preview/cancel/buy a connected freeform curve into vacant city frontage."""
    size=s.last_state["map_size"]
    width=min(64,size);origin=max(0,size//2-width//2)
    cells=[]
    for y in range(origin,origin+width,8):
        page=await s.request("inspect",{"section":"tiles","x":origin,"y":y,
            "width":width,"height":min(8,origin+width-y),"limit":512})
        cells.extend(page["items"])
    tiles={(t["x"],t["y"]):t for t in cells}
    sites={(f["x"],f["y"]) for f in facilities}
    candidates=[]
    for a,tile in tiles.items():
        if not tile["road"]:continue
        for dx,dy in ((0,1),(1,0),(0,-1),(-1,0)):
            for side in (-1,1):
                control=(a[0]+dx*3,a[1]+dy*3)
                b=(a[0]+dx*4-dy*side*3,a[1]+dy*4+dx*side*3)
                path=set()
                for step in range(65):
                    t=step/64
                    path.add(tuple(math.floor((1-t)**2*(a[i]+.5)+2*(1-t)*t*(control[i]+.5)+t*t*(b[i]+.5)) for i in (0,1)))
                if all(p==a or p in tiles and not tiles[p]["road"] and not tiles[p]["zone"] and
                    not tiles[p]["building_id"] and tiles[p]["type"]!=2 and p not in sites and
                    not tiles[p]["regional_connection_id"] for p in path):
                    candidates.append((a,control,b))
    assert candidates,"No vacant connected curved street site"
    candidates.sort(key=lambda v:(abs(v[0][0]-size//2)+abs(v[0][1]-size//2),v))
    before={k:s.last_state[k] for k in ("funds","road_tiles","tick")}
    alignments=s.last_state["tools"]["road_alignment_count"]

    async def gesture(a,control,b):
        if not s.last_state["tools"]["road"]:await player.key("R")
        for _ in range(4):
            if s.last_state["tools"]["road_layout"]=="CURVE":break
            await player.key("L")
        if s.last_state["tools"]["road_snap"]:await player.key("O")
        for tile in (a,control):
            point=await player.frame_tile(*tile)
            await player.click(*point)
        point=await player.frame_tile(*b)
        await player.move(*point)
        return point

    chosen=None
    for a,control,b in candidates[:24]:
        point=await gesture(a,control,b)
        quote=dict(s.last_state["preview"]["road"])
        if quote["valid"]:
            chosen=(a,control,b);break
        await player.key("Escape")
        assert all(s.last_state[k]==v for k,v in before.items())
    assert chosen,"Native previews rejected curved street candidates"
    await player.pause(2)
    await s.screenshot("curved-street-preview")
    await player.key("Escape")
    assert all(s.last_state[k]==v for k,v in before.items()),"Cancelled curve mutated city"
    await s.screenshot("curved-street-cancelled")
    point=await gesture(*chosen)
    assert s.last_state["preview"]["road"]["valid"]
    assert s.last_state["preview"]["road"]["cost"]==quote["cost"]
    await player.click(*point)
    assert before["funds"]-s.last_state["funds"]==quote["cost"]
    assert s.last_state["tools"]["road_alignment_count"]==alignments+1
    assert s.last_state["road_tiles"]>before["road_tiles"]
    await player.key("Escape")
    await player.orbit(await player.frame_tile(*chosen[-1]),dx=60,dy=-8)
    await player.pause(2)
    await s.screenshot("curved-street-built-orbit")
    await player.key("F5")
    snapshot=json.loads(s.artifact("urban_sim_session.json.city.json").read_text())
    path=[(p["x"],p["y"]) for p in snapshot["roadAlignments"][-1]["path"]]
    return {"path":path,"start":chosen[0],"control":chosen[1],"end":chosen[2],"cost":quote["cost"],
            "new_tiles":s.last_state["road_tiles"]-before["road_tiles"],"cancel_preserved_city":True}


async def construction(s,player,facilities):
    grid=await grid_construction(s,player,facilities)
    a,b=grid["start"],grid["end"]
    step=((b[0]-a[0])//2,(b[1]-a[1])//2)
    grid["pipes"]=await lay_road_pipes(s,player,[a,(a[0]+step[0],a[1]+step[1]),b])
    curve=await curve_construction(s,player,facilities)
    curve["pipes"]=await lay_road_pipes(s,player,curve["path"])
    await player.frame_tile(*curve["end"])
    await player.pause(2)
    await s.screenshot("curved-utilities-3d")
    await player.key("F3")
    await player.frame_tile(*curve["end"])
    await s.screenshot("curved-utilities-top-down")
    await player.key("F3")
    await player.key("Num1")
    mix={"grid":1,"non_right_angle":1,"grid_fraction":.5,
         "count_basis":"new street placements; excludes previews and cancellations"}
    s.artifact("road-mix.json").write_text(json.dumps(mix,indent=2))
    return {"grid":grid,"curve":curve,"road_mix":mix}


async def run(args):
    s = Session(args.executable, args.artifacts, timeout=120)
    player = PacedInput(s)
    visits = []
    try:
        await s.launch(mode="realtime")
        s.import_save(args.session)
        await record(s, "start")
        await player.key("L")
        if not s.last_state["paused"]:
            await player.key("Space")
        # No tool is active on load; keep the 3D context rather than going top-down.
        if s.last_state["projection"] == "top_down":
            await player.key("F3")
        await player.key("F")
        await player.moderate_zoom()
        await player.control("speed_1")
        if s.last_state["paused"]:
            await player.key("Space")
        exploration_start = s.last_state["tick"]
        await player.pause(2)
        await s.screenshot("overview")
        facilities = (await s.request("inspect", {"section":"facilities", "limit":512}))["items"]
        assert facilities, "Walkthrough requires a developed saved city"
        # Visit different services, with nearby sites first to avoid map-wide jumps.
        selected = []
        center = s.last_state["map_size"] // 2
        for f in sorted(facilities, key=lambda f: abs(f["x"]-center)+abs(f["y"]-center)):
            if f.get("type") not in [v.get("type") for v in selected]:
                selected.append(f)
            if len(selected) >= args.sites:
                break
        for i, f in enumerate(selected):
            p = await player.frame_tile(f["x"], f["y"])
            camera = s.last_state["camera"].copy()
            await player.orbit(p, dx=85 if i%2==0 else -70)
            assert s.last_state["camera"]["yaw"] != camera["yaw"], "Orbit did not engage"
            p = await player.frame_tile(f["x"], f["y"])
            await player.move(*p)
            hit = await s.request("coordinates", {"direction":"screen_to_visible_tile", "x":p[0], "y":p[1]})
            assert hit["x"] >= 0 and hit["y"] >= 0
            await player.pause()
            await s.screenshot(f"site-{i+1}-hover")
            await player.key("I")
            assert s.last_state["ui"]["pinned_tile"] == [hit["x"],hit["y"]]
            await player.pause(2.5)
            await s.screenshot(f"site-{i+1}-inspector")
            visits.append({"intended_site":f,"visible_tile":{k:hit[k] for k in ("x","y","valid")},"camera":s.last_state["camera"],
                           "tile_pixels":s.last_state["tile_pixels"]})
            await player.key("I")
            await player.pause()
        assert s.last_state["tick"] > exploration_start
        exploration_ticks = s.last_state["tick"]-exploration_start
        await player.key("Space")
        purchase = await construction(s, player, facilities)
        await player.control("budget")
        assert s.last_state["ui"]["budget"], "HUD budget control did not open"
        await player.pause(3)
        await s.screenshot("budget-reading")
        await player.key("F6")
        await player.control("speed_1")
        if s.last_state["paused"]:
            await player.key("Space")
        start = s.last_state["tick"]
        activity_before = None
        if args.check_activity:
            await s.request("status")
            activity_before = s.last_state["activity"]
            assert all(activity_before[k] > 0 for k in ("cars","trucks","pedestrians")), activity_before
            await s.screenshot("activity-moving-before")
        await player.pause(8)
        await s.request("status")
        assert s.last_state["tick"] > start, "Normal-speed simulation did not advance"
        activity_after = s.last_state.get("activity")
        if args.check_activity:
            assert activity_after["seconds"] > activity_before["seconds"]
            assert activity_after["sample"] != activity_before["sample"]
            await s.screenshot("activity-moving-after")
        await player.key("Space")
        if args.check_activity:
            await s.request("status")
            paused_activity = s.last_state["activity"]
            await player.pause(.8)
            await s.request("status")
            assert s.last_state["activity"] == paused_activity, "Visual traffic advanced while paused"
            projection = s.last_state["projection"]
            await player.key("F3")
            assert s.last_state["projection"] == "top_down"
            await player.key("F")
            await player.moderate_zoom()
            await player.frame_tile(selected[-1]["x"],selected[-1]["y"])
            await s.screenshot("activity-top-down")
            await player.key("F3")
            if s.last_state["projection"] != "perspective":
                await player.key("T")
            assert s.last_state["projection"] == "perspective"
            await player.key("F")
            await player.moderate_zoom()
            await s.screenshot("activity-perspective")
            if projection != "perspective":
                await player.key("T")
        await player.key("F5")
        await s.screenshot("after-observation")
        saved = {k:s.last_state[k] for k in ("tick","funds","road_tiles","paused")}
        await player.pause()
        await player.key("F9")
        assert all(s.last_state[k] == v for k,v in saved.items()), "Saved construction did not reload exactly"
        await s.screenshot("after-reload")
        assert s.last_state["validation_errors"] == 0
        s.artifact("walkthrough-result.json").write_text(json.dumps({
            "scope":"Paced scripted native exploration; visual review required; not a performance probe",
            "executable_sha256":hashlib.sha256(Path(args.executable).read_bytes()).hexdigest(),
            "checkpoint_sha256":hashlib.sha256(Path(args.session).read_bytes()).hexdigest(),
            "runner_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "paced_input_sha256":hashlib.sha256(Path(__file__).parents[1].joinpath("urban_sim_mcp/paced_input.py").read_bytes()).hexdigest(),
            "visits":visits,"observed_ticks":s.last_state["tick"]-start,
            "exploration_ticks":exploration_ticks,"construction":purchase,
            "saved_construction_reload_exact":True,
            "activity_check": {"before":activity_before,"after":activity_after,
                               "paused_exact":True} if args.check_activity else None,
            "validation_errors":s.last_state["validation_errors"]}, indent=2))
        await record(s, "stop")
        print(s.run_dir, flush=True)
    finally:
        await s.stop()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable", required=True)
    p.add_argument("--session", required=True)
    p.add_argument("--artifacts", default="playtest-artifacts/organic-walkthrough")
    p.add_argument("--sites", type=int, default=3)
    p.add_argument("--check-activity", action="store_true",
                   help="Require moving cars, freight trucks and pedestrians, plus exact pause")
    args = p.parse_args()
    if not 1 <= args.sites <= 6:
        p.error("sites must be between 1 and 6")
    asyncio.run(run(args))
