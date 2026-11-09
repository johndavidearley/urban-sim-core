# 3D renderer migration

The default city view now uses an orthographic orbit camera and procedural 3D
geometry in the existing Vulkan visualizer. `F3` switches to the top-down view.
Simulation coordinates, construction rules, city snapshots and budgets retain
their existing formats and behavior.

## Camera and tools

| Control | Action |
| --- | --- |
| Home / End | Rotate left / right by 15 degrees |
| Page Up / Page Down | Tilt up / down by 5 degrees, between 20 and 80 degrees |
| Middle mouse drag / arrows | Pan |
| Mouse wheel / + / - | Zoom |
| F | Center on developed land |
| F3 | Switch 3D / top-down |

Picking intersects an orthographic camera ray with the flat ground plane.
Construction previews use the same world-to-screen projection. Selected plots
remain visible through buildings so demolition and construction footprints are
readable. Inspecting a building uses its ground parcel. Orbiting keeps the
central ground parcel in view.

## Rendering

`Camera3D.hpp` owns projection and inverse ground picking. `Scene3D.cpp` builds
world-space terrain surfaces, roads and lane markings, building volumes,
windows, rooftops, service sites, trees, parks, transit marks and district
boundaries. Building heights follow capacity tiers. Surface elevation is zero
for land and slightly lower for water, with a solid base around the map.

World vertices carry projected Vulkan depth. Each swapchain image has its own
depth attachment, recreated with the swapchain during resize. The shader and
pipeline use that depth to resolve overlapping faces independently of draw
order. Screen-space UI and placement highlights use depth zero and retain their
existing painter order. CPU-side scene generation stays in the drawing library,
so headless builds have no Vulkan or GLFW dependency.

The playtest protocol reports projection `3d`. Camera actions accept `yaw`
(0 inclusive to 360 exclusive) and `pitch` (20–80). Retained `isometric`
projection requests alias `3d`; the historical isometric drawing helpers remain
available for graphics regression scenes. Both coordinate conversion directions
use the active camera.

## Current scope

This is the first working 3D rendering pass. The camera is orthographic;
terrain remains flat. Buildings, facilities and terminals use simple procedural
volumes rather than detailed assets. Perspective, terrain elevation, detailed
landmark and infrastructure models, dynamic lighting and shadow maps are
subsequent rendering work. Camera orientation is session-local.

## Validation

Verified locally November 9, 2026: strict Release, all 672 CTest cases, seven
Python unit tests, all 46 rendered cases and all 20 graphics color/depth probes
pass. Captures: `playtest-artifacts/3d-graphics/`; gameplay evidence:
`playtest-artifacts/3d-full/`. Hosted CI remains pending.

Camera tests cover picking at six yaws, four tilts and three zoom levels with
panning; height/depth behavior; and world/highlight vertex depth. A native
rendered test places roads at four yaws and tilts and checks coordinate round
trips. The graphics smoke captures four 3D angles after resize, alongside the
retained 16 historical scenes. Its depth probe submits a near red surface before
a distant blue surface, then puts green UI above both.

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
