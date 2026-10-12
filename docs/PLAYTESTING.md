# MCP desktop playtesting

The optional `UrbanSimCore-playtest` executable exposes the same Vulkan/GLFW
application through private process pipes. The Python `urban-sim-mcp` server
owns one game at a time and speaks MCP over stdio. Windows, Linux, and macOS use
the same game-side handlers. Normal GUI launches have no control endpoint.

## Build and connect

Install the graphics prerequisites described in `GRAPHICS.md`, then build:

```sh
cmake -S . -B build-playtest -DURBAN_SIM_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-playtest --config Debug --parallel 4
python -m venv .venv
# POSIX: source .venv/bin/activate
# Windows PowerShell: .venv\Scripts\Activate.ps1
python -m pip install ./playtest
urban-sim-mcp --executable /absolute/path/to/UrbanSimCore-playtest --artifacts /absolute/path/to/playtest-artifacts
```

Python 3.10+ is required. The package pins MCP SDK 1.26.0 and Pillow 11.3.0.
Python is not a CMake build dependency; `URBAN_SIM_BUILD_VISUALIZER=OFF` still
requires neither graphics dependencies nor Python. Windows multi-configuration
executables are under `bin/Debug` or `bin/Release`. `cmake --install` includes the executable and Python source package under
`share/urban-sim/playtest`; install that directory with pip when using an installed
layout. Use a Debug build with the
Khronos validation layer installed for renderer validation.

A generic stdio MCP client configuration:

```json
{
  "mcpServers": {
    "urban-sim": {
      "command": "/absolute/path/to/.venv/bin/urban-sim-mcp",
      "args": ["--executable", "/absolute/path/to/UrbanSimCore-playtest", "--artifacts", "/absolute/path/to/playtest-artifacts"]
    }
  }
}
```

On Windows use absolute paths to `.venv\\Scripts\\urban-sim-mcp.exe` and
`UrbanSimCore-playtest.exe`. The executable and server must run on the same OS:
a Windows game should use Windows Python, including when launched from WSL.
On Linux, pass the display-session environment (`DISPLAY`, or
`WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR`) through the MCP client to the server.
The client launches the server; launching the game is a separate `launch` tool.
The server never modifies client configuration automatically.

SDK copy-only Windows installations can expose layers with `VK_ADD_LAYER_PATH`
pointing to the SDK `Bin` directory. Explicit process-local `VK_LAYER_PATH`
is also supported. Stale third-party implicit-layer registrations can produce
loader errors; tests may set `VK_IMPLICIT_LAYER_PATH` to an empty directory for
an isolated renderer check. This is a test override, not a registry repair.

## Tools and coordinates

| Tool | Use |
|---|---|
| `launch`, `status`, `stop` | Own a fresh start-screen session, inspect lifecycle, save/discard and exit. |
| `input` | Ordered key, pointer, button, scroll, focus-loss and close events. |
| `window` | Native resize/minimize/restore/close; simulated focus loss. |
| `city` | New city (32/64/96), terrain, save/load, optional external save import. |
| `time` | Explicit 1–100 ticks, pause, speed, deterministic/realtime mode. |
| `tool` | Select/apply road, zoning, demolition, services, districts; district archetype/budget policies and deletion. |
| `view` | Eight overlays, both projections, camera, focus, HUD and gameplay toggles. |
| `inspect` | Summary/UI, <=512 tiles, or paginated entities/facilities/districts/routes. |
| `coordinates` | Tile-center to screen and screen to tile under the active projection. |
| `screenshot` | Next completed Vulkan frame as PNG image content plus metadata. |
| `diagnostics` | Last 1–500 log lines and renderer validation/memory/frame data. |
| `record`, `replay`, `assert_state` | Retain scenarios, replay into fresh processes, assert inspection fields. |

Read `urban-sim://controls` for key names, overlay ordering and action sources.
`urban-sim://capabilities`, `urban-sim://state`, `urban-sim://artifacts`, and
`urban-sim://artifact/{relative}` expose read-only evidence. Artifact names in
the resource template are URL-encoded and restricted to the current run.
Prompts `exploratory_playtest` and `regression_investigation` provide workflows.

`inspect(section="state")` includes `population_change` with `arrivals`,
`departures`, `deaths`, `net`, and `desirability`. These describe the **last
completed tick**, not the sum of a multi-tick step. Net change equals arrivals
minus departures minus deaths. Migration uses the previous completed tick's
conditions; edits to utilities take effect in those conditions after a tick.
Metrics also expose sewage, garbage, recycling, cemetery and crematorium
coverage, utility capacities/demand/supply ratios, and deathcare backlog.
Older saves initialize the new lagged inputs to defaults, then update them
on the first tick; their stored population target does not limit growth.

Input coordinates are **logical window pixels**, not framebuffer pixels.
Screenshot metadata includes both dimensions for DPI scaling. Placement tool
endpoints are tile coordinates. Use `inspect(section="ui")` for visible control
rectangles and `coordinates` instead of hardcoding positions or assuming a
28-pixel tile scale. Unsupported or off-screen coordinate conversions fail
clearly; valid tile centers can lie outside the current camera view.

For example, a click is a `down` and `up` with the same x/y; a drag is `down`,
`move`, `up`. Keys use names such as `Enter`, `R`, `F3`, `Num1`, `Period`, and
`Escape`. Scrolling includes x/y and delta so zoom anchors at the intended cursor.
These events traverse player handlers, but they are synthetic application
inputs: they do not test OS keyboard delivery or window-manager focus behavior.

Semantic `tool(action="apply", name="road", x1=4, y1=12, x2=10, y2=12)`
uses the same plan/build/refresh handler as a road drag, including costs and
placement errors. Zone, service and district names use the existing parsers.
City edits have no arbitrary state-write or cost-bypass facility. Traces label
input as `ui`, window operations as `window`, and other commands as `semantic`.

## Recording and evidence

Default deterministic sessions run simulation only through explicit ticks
(including the player step key while paused), with controlled animation time.
Realtime sessions retain normal wall-clock pause/speed behavior. Terrain and
simulation seeds default to 42 and 98765; custom seeds are unsigned 32-bit values.
The simulation seed offsets the existing playable tick seed and derived refresh
seed. Normal GUI simulation keeps its existing seed behavior.

Start `record(action="start")` immediately after `launch`, then create/load a
city, play, capture frames, and assert outcomes. Stop with a simple scenario name.
Assertions address dotted inspection fields, for example `state.tick eq 3`,
`state.funds ge 0`, or `items count 7`. Operators are `eq`, `count`, `lt`, `le`,
`gt`, `ge`; assertions never evaluate Python or C++ code.

`replay(path=..., repeats=2)` replays twice into fresh processes and compares a
canonical hash of the final saved gameplay session and city snapshot. State
comparison requires deterministic mode. Rendered pixel parity across GPUs is
not assumed. Scenarios retain configuration/build metadata, ordered actions,
delays, screenshot checkpoints, expected command failures, and assertions.
Imported saves are embedded in the recording so replay does not depend on the
original files. Traces retain actual results.

Each launch creates a distinct retained run directory containing `run.json`,
`actions.jsonl`, `game.log`, requested captures, run-local saves, and recorded
scenarios/replay reports. `city(new)` during gameplay creates a fresh process and
run directory; record reproducible scenarios from the start screen. External
loads copy the source session and referenced city snapshot into the run.
All generated writes stay in the run directory. Evidence is retained on success,
failure, timeout, cancellation, and disconnect.

Commands serialize on the server and execute on the game main thread. A bridge
failure or timeout invalidates the session and stops its process; a potentially
applied mutation is never automatically retried. MCP disconnect closes/reaps
the owned game after a bounded shutdown. Capture while minimized fails explicitly;
inspection, restore and close remain responsive. GLFW/Wayland cannot report
minimization or restore a minimized surface, so capabilities report
`window_minimize_restore=false` and those native operations fail explicitly;
use compositor controls for that hardware check. Initialization errors use the
bridge error response and stderr rather than a Windows modal dialog.

A ready-to-run example mixes player road dragging with semantic setup:

```sh
urban-sim-playtest playtest/examples/smoke.json --executable /absolute/path/to/UrbanSimCore-playtest --artifacts playtest-artifacts --repeats 2
```

## Tests

```sh
python -m unittest discover -s playtest/tests -p test_unit.py -v
# POSIX; use $env:NAME = 'value' in Windows PowerShell.
export URBAN_SIM_PLAYTEST_EXECUTABLE=/absolute/path/to/UrbanSimCore-playtest
export URBAN_SIM_PLAYTEST_ARTIFACTS=/absolute/path/to/playtest-artifacts
python -m unittest discover -s playtest/tests -p test_rendered.py -v
```

Rendered tests require a desktop/display and Vulkan driver. Linux CI runs these
under Xvfb and nested Wayland via `scripts/smoke_playtest_wayland.sh` and uploads
retained evidence. Run existing CTest and graphics capture checks as well.
Windows/macOS interactive and DPI/display acceptance remain hardware checks.
The existing rail-route simulation test failure is tracked in `GRAPHICS.md`;
MCP support does not change that simulation behavior.

## Verification — October 11, 2026

Windows/MSVC Debug with warnings treated as errors passed. On NVIDIA GeForce
GTX 1660 Ti, all seven Python unit tests and four rendered acceptance tests
passed. The SDK client exercised initialization, discovery, structured results,
PNG image responses, resources, prompts, invalid schemas, and disconnect cleanup.
Recorded city setup replayed twice with identical canonical saved state;
imported-save replay also passed. Validation layers were loaded and no Vulkan
validation errors were reported in the isolated layer environment.

The original graphics smoke passed all 16 capture probes. Existing CTest passed
373/374; `CitySimulatorTests.RailLinesAppearAlongsideBusesInALargeCity` remains
the sole failure, producing zero rail routes. Headless configuration succeeded
with Vulkan/GLFW package discovery disabled. The Windows installed layout and
shipped example CLI are included in acceptance checks.

Linux builds and local hardware runs were not available in that workspace.
Python unit checks and strict builds are configured on all desktop CI runners;
rendered X11/Wayland runs and retained evidence upload are configured on Linux.
Those CI executions remain to be verified. Wayland minimize/restore is
explicitly reported as unsupported by the GLFW backend rather than counted as
native coverage.

### macOS MCP verification

On macOS 15.8.1 with Intel Iris Plus 655 and MoltenVK, the strict Debug build
passed all 375 C++ tests and seven Python MCP unit tests. All four rendered
acceptance tests passed in both the build layout and installed layout. Installed
checks cleared `VK_DRIVER_FILES` and `VK_ICD_FILENAMES`, exercising bundled
loader/GLFW libraries and automatic bundled MoltenVK manifest discovery.
The installed visualizer also passed all 16 graphics capture probes, with
20 frames/20 draw calls and stable 4 MiB reusable geometry buffers.

The MCP SDK client verified discovery, image responses, resources, prompts,
invalid schemas, and disconnect cleanup. Rendered scenarios covered both
projections and all eight overlays, road/zoning/service/district actions,
save/load and imported saves, identical two-run replays, resize,
minimize/restore, close while minimized, focus-loss cancellation, insufficient
funds, and process-crash recovery.

Additional installed-bundle MCP checks verified start-screen button input,
coordinate-derived road dragging, logical 1280×800 to framebuffer 2560×1600
Retina scaling, cursor-anchored zoom, HUD pause/speed/save/load, step input,
large bitmap text, realtime ticks, and unsaved quit/cancel. Odd-height isometric
picking initially failed after zoom; inverse projection now uses the same
integer half extents as drawing, with CPU and rendered MCP regression coverage.

Retained evidence is under `playtest-artifacts/macos-build`,
`playtest-artifacts/macos-installed`, and
`playtest-artifacts/macos-installed-suite`; the supplemental SDK report is
`playtest-artifacts/macos-installed/macos-mcp-report.json`. These are local,
git-ignored artifacts. Logs and the supplemental SDK client script are retained
under `playtest-artifacts/macos-validation`. No Vulkan errors were reported,
but Khronos validation layers were unavailable on this Mac, so this run does not establish validation
layer coverage. Apple Silicon, cross-display scaling, and native OS input/focus
delivery remain separate hardware checks; MCP input is synthetic.
