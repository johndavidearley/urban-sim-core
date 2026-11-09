# 3D renderer migration

The city view uses an orbit camera and procedural 3D geometry in the existing
Vulkan visualizer. Orthographic is the default; `T` enables perspective. `F3`
switches to the top-down view.
Simulation coordinates, construction rules, city snapshots and budgets retain
their existing formats and behavior.

## Camera and tools

| Control | Action |
| --- | --- |
| Home / End | Rotate left / right by 15 degrees |
| Page Up / Page Down | Tilt up / down by 5 degrees, between 20 and 80 degrees |
| Right mouse drag, with no active construction tool | Orbit and tilt |
| Middle mouse drag / arrows | Pan |
| Mouse wheel / + / - | Zoom |
| F | Center on developed land |
| T | Switch orthographic / perspective; enter perspective from top-down |
| F3 | Switch 3D / top-down, retaining the last 3D projection |

Construction picking intersects the active camera ray with the flat ground plane. In
perspective, rays above the horizon or beyond the visible depth range return
an invalid tile, and construction ignores those points.
Construction previews use the same world-to-screen projection. Selected plots
remain visible through buildings so demolition and construction footprints are
readable. With no construction tool selected, hovering and the `I` inspector
key select the visible world surface and inspect its owning parcel. A roof
selects its building instead of ground behind it. Orbiting keeps the
exact central ground point fixed. Wheel zoom keeps the exact ground point under
the pointer fixed. Both use fractional offsets to avoid accumulated tile snaps.
Right-click retains its construction cancellation behavior while tools are active.

## Rendering

`Camera3D.hpp` owns projection and inverse ground picking. `Scene3D.cpp` builds
world-space terrain surfaces, roads and lane markings, building volumes,
windows, rooftops, service sites, trees, parks, transit marks and district
boundaries. Facilities now have type-specific procedural landmarks: fire bays,
a police flag, a hospital cross, a school tower, water towers, treatment tanks, cemetery
headstones, landfill mounds and utility structures. Solar panels fill their 2×2
plot; wind turbine blades rotate with animation time. Nuclear, hydro and
substation sites have separate forms. Airports use a runway, terminal and control
tower; cargo docks use containers and a crane, and passenger docks have a canopy.
Constructed waterfront terminals remain visible across water parcels.

Building heights follow capacity tiers. Surface elevation is zero
for land and slightly lower for water, with a solid base around the map.

World vertices carry projected Vulkan depth and homogeneous W. Perspective
geometry retains signed W so Vulkan clips faces crossing the camera or near
plane before dividing. Preview polygons are clipped to the near plane on the
CPU before receiving their overlay depth. Each swapchain image has its own
depth attachment, recreated with the swapchain during resize. The shader and
pipeline use that depth to resolve overlapping faces independently of draw
order. Screen-space UI and placement highlights use depth zero and retain their
existing painter order. CPU-side scene generation stays in the drawing library,
so headless builds have no Vulkan or GLFW dependency.

The playtest protocol reports projection `perspective` for perspective mode and
`3d` for orthographic. Camera actions accept `yaw`
(0 inclusive to 360 exclusive) and `pitch` (20–80). Retained `isometric`
projection requests alias `3d`; the historical isometric drawing helpers remain
available for graphics regression scenes. Both coordinate conversion directions
use the active camera.

## Building architecture

`BuildingModels3D.cpp` derives architecture from persistent building metadata,
capacity tier and a deterministic parcel hash. Entity IDs, occupancy and
animation time do not change a building's generated appearance. All geometry
stays within its simulation parcel; this pass changes no footprints, capacity,
costs, unlocks or city snapshot fields. Diagnostic overlays retain their data
colors on the same geometry.

| Building | Model |
| --- | --- |
| Residential, low density | Pitched roof, porch, door and separate window panes |
| Residential, higher density | Apartment block with balconies on all four sides |
| Commercial | Glazed storefront, striped awning and rooftop sign |
| Industrial | Twin shed roofs, loading bays and chimney |
| Office | Podium, glazed tower, setback roof and antenna |
| Hotel | Entrance canopy and rooftop H; closed hotels use a muted sign |

Treasury Hall has steps, columns and a pediment. Transit Exchange has two
platform canopies and a station clock. Trade Hall has market sheds and a loading
apron. Visitor Pavilion has an open dome canopy and information kiosk. City
Academy has classroom wings and a clock tower. Civic Square has a fountain,
statue and benches.

Medical Citadel has three clinical towers and a rooftop cross. Universal Campus
has research wings and an observatory. Clean Energy Spire is a tapered tower
with a solar crown. Ecology Dome has a faceted greenhouse on a planted base.

`Scene3DPrimitives.hpp` shares triangle, quad, box and slab emission between
architecture and infrastructure, including signed perspective W and clipped
placement highlights. Gables and dome caps use actual triangles. Materials and
roof orientation vary by parcel, while the silhouettes of all ten civic
landmarks are distinct.

## Visible surface inspection

`ScenePicking3D` collects the same world triangles used to draw terrain,
buildings, vegetation, facilities and terminals. It groups consecutive parcels
into world-space bounding boxes, then traces the camera ray against actual
triangles in candidate groups. The nearest hit within the render depth range
wins, including occlusion by nonselectable map edges. Open spaces in a canopy
or between model parts do not become solid selection boxes.

Hover uses the current frame's collection. Keyboard pinning and explicit tool
queries rebuild from current geometry, so camera, construction or load changes
in the same input batch do not use stale screen data. Facility geometry resolves
to its site anchor, including the whole solar footprint; airport and dock parts
resolve to the terminal origin. Perspective inspection can select a visible
skyline even if the corresponding ground ray points above the horizon.

Construction previews and screen-space HUD geometry never enter the pick
collection. All construction and demolition tools retain ground-grid picking;
using the inspector does not change a plan's target or city state. The pick
collection is CPU-only and does not change the GPU vertex format.

## Current scope

The foundation, infrastructure, perspective and architecture passes are implemented. Both
camera projections use the same flat terrain. Buildings, facilities and
terminals use procedural volumes rather than imported assets. Terrain elevation,
more detailed assets, dynamic lighting and shadow maps are
subsequent rendering work. Camera orientation is session-local.

## Validation

Verified locally November 9, 2026: strict Release, all 689 CTest cases, seven
Python unit tests, all 49 rendered cases and all 40 graphics color/depth probes
pass. Captures: `playtest-artifacts/visible-picking-graphics/`; gameplay evidence:
`playtest-artifacts/visible-picking-full/` and `playtest-artifacts/visible-picking-native/`. Hosted CI remains pending.

Camera tests cover picking at six yaws, four tilts and three zoom levels with
panning; height/depth behavior; and world/highlight vertex depth. A native
rendered test places roads at four yaws and tilts and checks coordinate round
trips. Further tests cover repeated orbit/zoom without pivot drift, solar plot
bounds, visible water above the map base, and constructed terminals across water
parcels. Native gesture checks cover right-drag, wheel zoom, construction
cancellation, view switching and tool shortcuts during a drag. The graphics
smoke captures four infrastructure views plus four original 3D angles after
resize, alongside the retained 16 historical scenes. Four perspective views and
four close-camera clipping scenes extend that suite to 32 captures. Eight
architecture gallery scenes add all building types, six capacity tiers, open and
closed hotels, six unique buildings and four monuments at four angles in each
camera projection, bringing the full smoke run to 40 captures. Its depth
probe submits a near red surface before a distant blue surface, then puts green
UI above both. Additional camera tests cover perspective foreshortening, inverse
ray picking, sky rejection, repeated pivot transforms, signed homogeneous W and
near-clipped construction previews. Architecture tests cover parcel bounds,
capacity-driven skyline growth, appearance independent of IDs and occupancy,
distinct landmark meshes, diagnostic geometry, and finite faces near the camera.
Picking tests cover nearest surfaces, triangle holes, site anchors, sky-facing
inspection, near/far clipping and exclusion of previews. The native roof test
checks inspection and ground-based road construction at four yaws in each
projection. The native perspective case builds roads at
four camera angles and exercises the projection toggle, orbiting and wheel zoom.

```sh
cmake --build build-strict -j 4
ctest --test-dir build-strict --output-on-failure -j 4
build-strict/bin/UrbanSimCore-visualizer --graphics-smoke playtest-artifacts/3d-graphics
python scripts/check_graphics_capture.py playtest-artifacts/3d-graphics
PYTHONPATH=playtest URBAN_SIM_PLAYTEST_EXECUTABLE=build-strict/bin/UrbanSimCore-playtest \
  python -m unittest discover -s playtest/tests -p test_rendered.py
```

On macOS, use the configured Vulkan/MoltenVK environment and allow native windows
for the rendered checks.
