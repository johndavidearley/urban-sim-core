"""MCP stdio entry point. Protocol output is owned exclusively by the SDK."""

from __future__ import annotations
import argparse
import base64
import json
import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Annotated, Any, Literal
import anyio
from pydantic import BaseModel, ConfigDict, Field
from mcp.server.fastmcp import FastMCP
from mcp.types import CallToolResult, ImageContent, TextContent, ToolAnnotations
from .session import PlaytestError, Session
from . import scenarios, __version__

UInt32 = Annotated[int, Field(ge=0, le=4294967295)]
Coordinate = Annotated[int, Field(ge=0, le=16384)]
KEYS = [
    "Escape",
    "Enter",
    "Tab",
    "Home",
    "End",
    "I",
    "Space",
    "Period",
    "Equal",
    "Plus",
    "Minus",
    "Left",
    "Right",
    "Up",
    "Down",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    *[f"Num{i}" for i in range(1, 10)],
    *list("ABCDFGHJKLNOPRSTUVZ"),
]


class InputEvent(BaseModel):
    model_config = ConfigDict(extra="forbid")
    type: Literal["key", "move", "down", "up", "scroll", "focus_lost", "close"]
    key: str | None = None
    repeat: bool = False
    x: Coordinate = 0
    y: Coordinate = 0
    button: Literal["left", "middle", "right"] = "left"
    delta: Annotated[float, Field(ge=-100, le=100, allow_inf_nan=False)] = 0


def compact(**values):
    return {key: value for key, value in values.items() if value is not None}


INSPECTION_REFERENCE = {
    "inspection_version": 3,
    "diagnostics": {"scope": "Current local growth gates, last evaluated demand, and a radius-4 (up to 9x9) neighborhood",
        "weights": "crime/illness resident-weighted; service reach building-weighted",
        "limits": "No local unemployment inference or guaranteed growth; utility flags are delivered connectivity, services_mask is road reach"},
    "sections": [
        "summary",
        "diagnostics",
        "ui",
        "tiles",
        "entities",
        "facilities",
        "districts",
        "routes",
        "regional_connections",
        "regional_report",
        "regional_alerts",
        "tourism",
        "roads",
    ],
    "summary": {
        "path": "state",
        "population_change": {
            "fields": ["arrivals", "departures", "deaths", "net", "desirability"],
            "units": "residents, except desirability (0..1)",
            "period": "last completed tick, not the sum of a multi-tick step",
            "identity": "net = arrivals - departures - deaths",
            "lag": "migration uses previous completed tick conditions",
        },
        "metrics": {
            "path": "state.metrics",
            "city_rates": ["crime_rate", "illness_rate"],
            "city_rate_weighting": "occupied residential population; empty city reports zero",
            "coverage": [
                "fire_coverage",
                "police_coverage",
                "health_coverage",
                "education_coverage",
                "power_coverage",
                "water_coverage",
                "sanitation_coverage",
                "garbage_coverage",
                "recycling_coverage",
                "cemetery_coverage",
                "crematorium_coverage",
            ],
            "utility_supply": [
                "power_demand_mw",
                "power_supply_mw",
                "power_supply_ratio",
                "water_demand",
                "water_supply",
                "water_supply_ratio",
            ],
            "deathcare_backlog": "residents awaiting disposition",
            "fraction_units": "coverage, supply ratios and rates are 0..1",
        },
    },
    "roads": {
        "fields": [
            "x1",
            "y1",
            "x2",
            "y2",
            "road_class",
            "capacity",
            "load",
            "freight_load",
            "congestion",
        ],
        "capacity": "local=10, arterial=40 vehicles/tick",
        "load": "commuters plus freight; freight_load is trucks only",
        "upgrade": "tool(action=apply,name=road,road_class=arterial); $300 per existing local segment",
    },
    "freight": {
        "path": "state.metrics",
        "fields": [
            "freight_trucks",
            "freight_routes",
            "freight_domestic_goods",
            "freight_imported_goods",
            "freight_exported_goods",
            "freight_unrouted_goods",
            "freight_gate",
        ],
        "period": "last traffic pass, not accumulated across a multi-tick step",
        "batch": "32 goods per truck; unconnected shipments are counted as unrouted goods",
        "gate": "fixed west edge at (0,map height/2); requires road access",
    },
    "facilities": {
        "common": ["type", "x", "y", "quality", "distance"],
        "power_only": ["power_source", "power_capacity_mw", "emissions_kg_per_mwh"],
        "water_only": ["water_supply"],
    },
    "local_rates": {
        "fields": ["crime_rate", "illness_rate"],
        "units": "fractions 0..1",
        "tiles": "present only on residential building tiles",
        "entities": "residential read-outs; other building types default to zero",
        "timing": "cached completed read-outs; update on ticks, land value uses lagged rates",
        "effects": "police road distance lowers crime; hospital/sewage road distance and pollution affect illness",
    },
    "examples": [
        {"tool": "inspect", "arguments": {"section": "summary"}},
        {"tool": "inspect", "arguments": {"section": "entities", "limit": 512}},
        {
            "tool": "assert_state",
            "arguments": {
                "field": "state.population_change.net",
                "op": "ge",
                "expected": 0,
            },
        },
    ],
}


def create_server(session: Session) -> FastMCP:
    @asynccontextmanager
    async def lifespan(_server):
        try:
            yield {}
        finally:
            # MCP EOF/cancellation must still reap the owned process.
            with anyio.CancelScope(shield=True):
                await session._terminate()

    server = FastMCP(
        "UrbanSimCore Playtesting",
        lifespan=lifespan,
        instructions="Launch an isolated session, inspect state and controls, then play. "
        "Coordinates are logical window pixels. Use explicit ticks for deterministic tests. "
        "UI and semantic gameplay actions are labeled separately in retained traces. "
        "Read urban-sim://inspection for population flow, utility/service metrics and neighborhood rates.",
    )
    readonly = ToolAnnotations(
        readOnlyHint=True, destructiveHint=False, openWorldHint=False
    )
    local = ToolAnnotations(
        readOnlyHint=False, destructiveHint=False, openWorldHint=False
    )

    @server.tool(annotations=local)
    async def launch(
        terrain_seed: UInt32 = 42,
        simulation_seed: UInt32 = 98765,
        mode: Literal["deterministic", "realtime"] = "deterministic",
    ) -> dict[str, Any]:
        """Launch one isolated rendered game at its start screen; returns capabilities and run directory."""
        return await session.launch(terrain_seed, simulation_seed, mode)

    @server.tool(annotations=readonly)
    async def status() -> dict[str, Any]:
        """Read current phase, tick, state revision, dimensions, process and retained artifact directory."""
        if (
            not session.process
            or session.broken
            or session.process.returncode is not None
        ):
            return {
                "running": False,
                "server_version": __version__,
                "broken": session.broken,
                "run_directory": str(session.run_dir) if session.run_dir else None,
            }
        return {
            **await session.request("status"),
            "running": True,
            "server_version": __version__,
            "pid": session.process.pid,
            "run_directory": str(session.run_dir),
        }

    @server.tool(annotations=local)
    async def stop(action: Literal["save", "discard"] = "discard") -> dict[str, Any]:
        """Save or discard the owned session and stop its process; always retain artifacts."""
        return await session.stop(action)

    @server.tool(annotations=local)
    async def input(
        events: Annotated[list[InputEvent], Field(min_length=1, max_length=256)],
    ) -> dict[str, Any]:
        """Inject ordered player input. Click=down/up, drag=down/move/up. Keys use names in controls resource."""
        for event in events:
            if event.type == "key" and event.key not in KEYS:
                raise PlaytestError(f"Unknown key; valid keys: {', '.join(KEYS)}")
        return await session.request(
            "input", {"events": [e.model_dump(exclude_none=True) for e in events]}
        )

    @server.tool(annotations=local)
    async def window(
        action: Literal["resize", "minimize", "restore", "focus_lost", "close"],
        width: Annotated[int, Field(ge=800, le=4096)] | None = None,
        height: Annotated[int, Field(ge=640, le=4096)] | None = None,
    ) -> dict[str, Any]:
        """Operate the native window; focus_lost is simulated and close follows the player quit dialog."""
        return await session.request(
            "window", compact(action=action, width=width, height=height)
        )

    @server.tool(annotations=local)
    async def city(
        action: Literal["new", "load", "save"],
        size: Literal[32, 64, 96] = 64,
        terrain: bool = False,
        import_path: str | None = None,
    ) -> dict[str, Any]:
        """Create a city, save, or load; optional import copies the source save and snapshot into the run. New during gameplay launches a fresh isolated process."""
        return await session.city(action, size, terrain, import_path)

    @server.tool(annotations=local)
    async def time(
        action: Literal["step", "pause", "speed", "mode"],
        ticks: Annotated[int, Field(ge=1, le=100)] = 1,
        paused: bool = True,
        speed: Literal[1, 2, 3] | None = None,
        mode: Literal["deterministic", "realtime"] | None = None,
    ) -> dict[str, Any]:
        """Advance explicit ticks or set pause, speed (1X/2X/3X), and deterministic/realtime mode."""
        return await session.request(
            "time",
            compact(action=action, ticks=ticks, paused=paused, speed=speed, mode=mode),
        )

    @server.tool(annotations=local)
    async def tool(
        action: Literal["select", "apply", "district_policy", "district_delete"],
        name: (
            Literal["road", "zone", "bulldoze", "service", "district", "port", "airport", "hotel", "none"] | None
        ) = None,
        x1: Coordinate = 0,
        y1: Coordinate = 0,
        x2: Coordinate | None = None,
        y2: Coordinate | None = None,
        zone: str | None = None,
        service: str | None = None,
        surface_water: bool | None = None,
        archetype: str | None = None,
        road_class: Literal["local", "arterial"] | None = None,
        id: UInt32 | None = None,
        budget_cap: int | None = None,
        port_action: Literal["cargo", "passenger", "remove", "toggle", "capacity", "airport"] | None = None,
        capacity: Annotated[int, Field(ge=0, le=200)] | None = None,
    ) -> dict[str, Any]:
        """Use gameplay tools with normal plans, costs and validation. Endpoints are tile coordinates; services use x1/y1; surface_water selects shoreline intake/raw outlet variants. Road class local/arterial supports build or upgrade. Policies support archetype and budget cap. Hotel uses x1/y1 on empty unzoned land with road/power/water; $10000, 32 beds, 12 commercial jobs; building enables tourism; visitors require reachable parks. Terminal tools use x1/y1; name airport defaults to 4x2 airport placement, while port_action selects cargo/passenger/airport/remove/toggle/capacity. Capacity sets 0..200 goods, 0..60 sea passengers or 0..120 air passengers; omission cycles presets."""
        return await session.request(
            "tool",
            compact(
                action=action,
                name=name,
                x1=x1,
                y1=y1,
                x2=x2,
                y2=y2,
                zone=zone,
                service=service,
                surface_water=surface_water,
                archetype=archetype,
                road_class=road_class,
                id=id,
                budget_cap=budget_cap,
                port_action=port_action,
                capacity=capacity,
            ),
        )

    @server.tool(annotations=local)
    async def budget(
        action: Literal["inspect", "set_taxes", "set_funding", "reset_funding", "reset", "open", "close", "services", "taxes", "trends", "alerts", "view_alert", "transport", "set_route_vehicles", "set_tourism", "set_hotel"] = "inspect",
        alert_index: Annotated[int, Field(ge=0, le=4)] | None = None,
        route_id: Annotated[int, Field(ge=0, le=4294967295)] | None = None,
        vehicles: Annotated[int, Field(ge=0, le=20)] | None = None,
        tourism_enabled: bool | None = None,
        hotel_id: Annotated[int, Field(ge=1, le=4294967295)] | None = None,
        hotel_open: bool | None = None,
        tax_percent: Annotated[
            list[Annotated[int, Field(ge=0, le=20)]], Field(min_length=5, max_length=5)
        ] | None = None,
        service_percent: Annotated[
            list[Annotated[int, Field(ge=0, le=100)]], Field(min_length=11, max_length=11)
        ] | None = None,
    ) -> dict[str, Any]:
        """Inspect/open the city budget or set integer tax percentages (R,C,I,O,income).

        Defaults are [5,8,6,9,10]; allowed range is 0..20. Taxes affect revenue.
        The next-tick estimate excludes new growth, visitors and debt flows. Last-tick
        ledger is immutable until the next simulation tick. Service funding is
        ordered Fire, Police, Health, Education, Power, Water, Sanitation,
        Garbage, Recycling, Cemetery, Crematorium; 0..100%, default all 100.
        Cuts reduce reach, utility supply, and processing capacity; zero closes
        operation without removing facilities. reset restores the entire budget;
        reset_funding preserves taxes. services/taxes/trends select the panel tab. History contains the latest
        240 completed ticks, including the policy used for each sample.
        alerts opens completed-tick diagnostics; view_alert follows its action
        (index 0 power, 1 water, 2 unemployment, 3 services, 4 budget).
        transport opens route management. set_route_vehicles edits a bus route
        fleet (0..20), with $2 per assigned vehicle per tick; completed usage
        remains immutable until stepping. set_tourism with tourism_enabled changes
        visitor policy. set_hotel with hotel_id and hotel_open controls one hotel;
        closing releases guests next tick while jobs and upkeep continue.
        inspect tourism returns policy, active stays, pending
        departures and immutable completed demand/travel/accounting reports.
        Municipal fees are $2 per admitted day visitor and $4 per occupied bed
        each tick, with $1 services per day visitor/occupied bed. Separate
        tourism_fees/tourism_services reconcile to last_tick treasury totals.
        """
        return await session.request("budget", compact(action=action, alert_index=alert_index, route_id=route_id, vehicles=vehicles, tourism_enabled=tourism_enabled, hotel_id=hotel_id, hotel_open=hotel_open, tax_percent=tax_percent, service_percent=service_percent))

    @server.tool(annotations=local)
    async def view(
        action: Literal["overlay", "projection", "camera", "focus", "toggle"],
        overlay: Annotated[int, Field(ge=0, le=8)] | None = None,
        projection: Literal["isometric", "top_down"] | None = None,
        tile_pixels: Annotated[int, Field(ge=2, le=48)] | None = None,
        view_x: Coordinate | None = None,
        view_y: Coordinate | None = None,
        offset_x: Annotated[int, Field(ge=-100000, le=100000)] | None = None,
        offset_y: Annotated[int, Field(ge=-100000, le=100000)] | None = None,
        name: (
            Literal[
                "legend", "compact_ui", "large_text", "onboarding", "autonomous_growth", "disasters"
            ]
            | None
        ) = None,
        enabled: bool | None = None,
    ) -> dict[str, Any]:
        """Choose overlay/projection, focus or position camera, or set an existing gameplay/UI toggle."""
        return await session.request(
            "view",
            compact(
                action=action,
                overlay=overlay,
                projection=projection,
                tile_pixels=tile_pixels,
                view_x=view_x,
                view_y=view_y,
                offset_x=offset_x,
                offset_y=offset_y,
                name=name,
                enabled=enabled,
            ),
        )

    @server.tool(annotations=readonly)
    async def inspect(
        section: Literal[
            "summary",
            "diagnostics",
            "ui",
            "tiles",
            "entities",
            "facilities",
            "districts",
            "routes",
            "regional_connections",
            "regional_report",
            "regional_alerts",
            "tourism",
            "roads",
        ] = "summary",
        offset: Annotated[int, Field(ge=0)] = 0,
        limit: Annotated[int, Field(ge=1, le=512)] = 128,
        x: Coordinate = 0,
        y: Coordinate = 0,
        width: Annotated[int, Field(ge=1, le=96)] = 1,
        height: Annotated[int, Field(ge=1, le=96)] = 1,
    ) -> dict[str, Any]:
        """Read summary state (including last-tick population_change and city metrics), UI bounds, <=512 tiles or paginated collections. Residential tiles/entities expose crime_rate and illness_rate. diagnostics at x/y reports local growth gates and a radius-4 neighborhood; it does not predict seeded growth or local employment. See urban-sim://inspection for units, weighting and timing."""
        return await session.request(
            "inspect",
            dict(
                section=section,
                offset=offset,
                limit=limit,
                x=x,
                y=y,
                width=width,
                height=height,
            ),
        )

    @server.tool(annotations=readonly)
    async def coordinates(
        direction: Literal["tile_to_screen", "screen_to_tile"],
        x: Coordinate,
        y: Coordinate,
    ) -> dict[str, Any]:
        """Convert tile centers and logical window coordinates using the active camera/projection."""
        return await session.request("coordinates", dict(direction=direction, x=x, y=y))

    @server.tool(annotations=local, structured_output=False)
    async def screenshot(name: str = "frame") -> CallToolResult:
        """Capture the next completed Vulkan framebuffer as a PNG image with frame and state metadata."""
        result = await session.screenshot(name)
        return CallToolResult(
            content=[
                TextContent(type="text", text=json.dumps(result)),
                ImageContent(
                    type="image",
                    mimeType="image/png",
                    data=base64.b64encode(Path(result["png"]).read_bytes()).decode(),
                ),
            ],
            structuredContent=result,
        )

    @server.tool(annotations=readonly)
    async def diagnostics(
        lines: Annotated[int, Field(ge=1, le=500)] = 100,
    ) -> dict[str, Any]:
        """Read bounded game logs plus renderer validation errors, memory capacity, and frame dimensions."""
        result = (
            await session.request("diagnostics")
            if session.process
            and not session.broken
            and session.process.returncode is None
            else {"state": session.last_state, "broken": session.broken}
        )
        return {**result, "logs": session.logs(lines)}

    @server.tool(annotations=local)
    async def record(
        action: Literal["start", "stop"], name: str = "scenario"
    ) -> dict[str, Any]:
        """Record from a fresh start screen; stop writes a versioned scenario into retained artifacts."""
        return await scenarios.record(session, action, name)

    @server.tool(annotations=local)
    async def replay(path: str, repeats: Literal[1, 2] = 1) -> dict[str, Any]:
        """Replay a recorded scenario into fresh sessions; two deterministic runs must have identical canonical city state."""
        return await scenarios.replay(session, path, repeats)

    @server.tool(annotations=readonly)
    async def assert_state(
        field: str,
        op: Literal["eq", "count", "lt", "le", "gt", "ge"],
        expected: Any,
        section: Literal[
            "summary",
            "diagnostics",
            "ui",
            "tiles",
            "entities",
            "facilities",
            "districts",
            "routes",
            "regional_connections",
            "regional_report",
            "regional_alerts",
            "tourism",
            "roads",
        ] = "summary",
        query: dict | None = None,
    ) -> dict[str, Any]:
        """Assert a documented dotted inspection field, e.g. state.tick eq 1 or items count 7; records the assertion."""
        return await scenarios.assert_state(
            session, field, op, expected, section, query
        )

    @server.resource("urban-sim://inspection")
    def inspection_reference() -> str:
        """Inspection fields, units, timing, weighting and assertion examples."""
        return json.dumps(INSPECTION_REFERENCE)

    @server.resource("urban-sim://controls")
    def controls() -> str:
        return json.dumps(
            {
                "keys": KEYS,
                "coordinates": "logical window pixels; placements use tile coordinates",
                "overlays": [
                    "zone",
                    "land_value",
                    "pollution",
                    "service_coverage",
                    "traffic_congestion",
                    "demand",
                    "happiness",
                    "route_heatmap",
                    "transit",
                ],
                "start_screen": {"new": "Enter", "load": "L", "terrain": "T toggles lakes/river (default) or flat map"},
                "gameplay": {
                    "road": "R",
                    "road_class_toggle": "A (road tool active)",
                    "zone": "Z",
                    "bulldoze": "B",
                    "service": "S",
                    "shore_water": "V toggles inland/shore mode while Water or Sanitation is selected; natural new maps default to shore",
                    "district": "P",
                    "airport": "J or Airport button builds a 4x2 passenger airport; V manages terminals",
                    "hotel": "K or Hotel button places a $10000 hotel with 32 beds and 12 commercial jobs; B removes it",
                    "connections": "U or Connections button; terminal/hotel tabs, operation and map location; Tab/Enter controls, arrows select, Space pauses, N steps",
                    "ports": "V cycles cargo/passenger/remove/on-off/capacity; click a site; Escape cancels",
                    "pause": "Space",
                    "step": "Period",
                    "save": "F5",
                    "load": "F9",
                    "projection": "F3",
                    "quit": "Escape",
                },
                "input_sources": "input=synthetic player events; tool/city/time/view=semantic; window=native except simulated focus_lost",
            }
        )

    @server.resource("urban-sim://capabilities")
    def capabilities() -> str:
        return json.dumps(
            session.capabilities or {"bridge_version": 1, "launch_required": True}
        )

    @server.resource("urban-sim://state")
    async def current_state() -> str:
        return json.dumps(await status())

    @server.resource("urban-sim://artifacts")
    def artifacts() -> str:
        return json.dumps(
            {
                "run_directory": str(session.run_dir) if session.run_dir else None,
                "files": (
                    [
                        str(p.relative_to(session.run_dir))
                        for p in session.run_dir.rglob("*")
                        if p.is_file()
                    ][:1000]
                    if session.run_dir
                    else []
                ),
            }
        )

    @server.resource("urban-sim://artifact/{relative}")
    def artifact(relative: str) -> bytes:
        import urllib.parse

        path = session.artifact(urllib.parse.unquote(relative))
        if path.stat().st_size > 4 * 1024 * 1024:
            raise PlaytestError(
                "Resource exceeds 4 MiB; use screenshot or retained file"
            )
        return path.read_bytes()

    @server.prompt()
    def exploratory_playtest(goal: str = "Find gameplay and visual issues") -> str:
        return f"{goal}. Launch an isolated game, read urban-sim://controls and urban-sim://inspection, inspect visible control bounds, play through input, and take screenshots. Use state inspection to verify effects. Label semantic setup separately from UI coverage. Retain the run directory and report reproducible steps and evidence."

    @server.prompt()
    def regression_investigation(symptom: str) -> str:
        return f"Investigate: {symptom}. Launch deterministically, record from the start screen, reproduce through player inputs, add state assertions and screenshot checkpoints, and stop recording. Replay twice and compare canonical city state. Report failing assertions and renderer diagnostics with retained artifacts."

    return server


def main():
    parser = argparse.ArgumentParser(description="UrbanSimCore local stdio MCP server")
    parser.add_argument(
        "--executable", default=os.environ.get("URBAN_SIM_PLAYTEST_EXECUTABLE")
    )
    parser.add_argument(
        "--artifacts",
        default=os.environ.get("URBAN_SIM_PLAYTEST_ARTIFACTS", "playtest-artifacts"),
    )
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if not args.executable:
        parser.error("--executable or URBAN_SIM_PLAYTEST_EXECUTABLE is required")
    if not 1 <= args.timeout <= 300:
        parser.error("--timeout must be 1..300 seconds")
    create_server(Session(args.executable, args.artifacts, args.timeout)).run(
        transport="stdio"
    )


if __name__ == "__main__":
    main()
