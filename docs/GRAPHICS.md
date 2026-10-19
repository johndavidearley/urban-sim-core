# GLFW/Vulkan visualizer

The simulation and saves are independent of the window and renderer. The
visualizer uses GLFW 3.4+ and Vulkan 1.0 features, with MoltenVK on macOS.
The drawing library is CPU-only; the optional host owns all graphics resources.
The initial window remains 1280×800 and can now be resized, with an 800×640
minimum to keep the existing start-screen controls accessible.

## Build and install

Follow the prerequisites in the README. Set `VULKAN_SDK` to the SDK root if
CMake cannot locate Vulkan or `glslc`; Homebrew installations are also supported.
A headless build (`URBAN_SIM_BUILD_VISUALIZER=OFF`) requires neither Vulkan,
GLFW, MoltenVK, nor a shader compiler. The GLFW fallback builds both X11 and
Wayland on Linux; an externally supplied package should support both too.

Run `cmake --install build --prefix ./install` (plus `--config Release` for
multi-configuration generators). Windows and Linux require the system Vulkan
loader and a Vulkan-capable graphics driver. Linux software rendering through
Mesa lavapipe is useful for testing. Shared GLFW packages must also be available
at runtime on Linux; source-fetched GLFW is static by default.

On macOS the install step copies and ad-hoc signs the loader, MoltenVK, and
shared GLFW if applicable. Their paths are relative to the installed executable.
The host selects the bundled driver manifest unless `VK_DRIVER_FILES` or
`VK_ICD_FILENAMES` is explicitly configured. Keep `bin` and `lib` together.
Distribution signing/notarization remains a separate release step.

## Automated checks

```bash
cmake -S . -B build-graphics -DCMAKE_BUILD_TYPE=Debug -DURBAN_SIM_WARNINGS_AS_ERRORS=ON
cmake --build build-graphics --parallel
ctest --test-dir build-graphics --output-on-failure
./build-graphics/bin/UrbanSimCore-visualizer --graphics-smoke ./graphics-captures
python3 scripts/check_graphics_capture.py ./graphics-captures
```

The bounded smoke mode uses seeded terrain, fixed animation time, representative
buildings, every overlay, both projections, the HUD, and explicit alpha/order
probes. It changes window sizes, captures 16 PPM frames, and checks stable buffer
capacity after warm-up. It prints vertex count, buffer capacity, and total time;
total time includes vsync and capture I/O and is not a renderer benchmark.
For a bounded 240-frame isometric measurement without captures, use
`--graphics-benchmark` in a Release build. It reports median frame and CPU
geometry-generation times after ten warm-up frames; vsync remains enabled.
Use `--reference DIRECTORY` with the Python checker to compare same-resolution
reference frames (default channel tolerance 3, differing-pixel allowance 1%).

Debug builds enable Khronos validation and synchronization validation if installed.
They remain usable without layers. Linux CI installs the layers and runs the
smoke mode under Xvfb and a Weston compositor nested on X11. The nested X11
backend supplies the input seat GLFW 3.4 requires; Weston's seatless headless
backend is unsuitable for this GLFW version. An X11 control smoke
also exercises actual road/zoning drags, stepping, save/load, overlays, focus-loss
cancellation, minimize/restore, resize, and quit/cancel in a temporary save
directory (`scripts/smoke_visualizer_controls.sh`, requiring xdotool, wmctrl, and Openbox). On macOS/Homebrew:

```bash
brew install vulkan-validationlayers
export VK_LAYER_PATH="$(brew --prefix vulkan-validationlayers)/share/vulkan/explicit_layer.d"
export VK_DRIVER_FILES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json"
```

Smoke mode fails on validation errors. CPU tests cover geometry area, draw order,
alpha retention, font-atlas bounds, case/punctuation, proportional spacing, and
logical-coordinate hit testing. Existing
simulation and persistence tests continue unchanged.

## UI typography

The UI embeds Roboto Condensed Medium (SIL OFL 1.1), replacing the hand-drawn
3×5 alphabet. Text preserves case and printable ASCII punctuation. Compact
labels use 11 logical pixels, readable labels 14, and headings 18/24/32; F2
switches label size. Glyphs are baked at twice the logical resolution and sampled
with linear filtering for smooth edges on normal and high-density displays.

`tools/generate_font_atlas.py` regenerates `UiFontAtlas.hpp` using Pillow and the
vendored original font in `assets/fonts/`. Ordinary builds and installed games
have no font-library or filesystem dependency. The Vulkan backend uploads one
R8 atlas during initialization; glyphs are textured quads in the same ordered
batch as solid geometry. The atlas survives swapchain recreation and is released
with the renderer. The font attribution/license are installed in
`share/urban-sim/fonts`.

Local font verification (October 19, 2026): strict Clang build, all 650 CTest
cases, all 42 rendered cases, and all 16 graphics smoke capture checks passed.
Minimum-window menu, Services, inspector and compact HUD were visually checked.
Evidence: `playtest-artifacts/fonts/REPORT.md`.

## Hardware acceptance checklist

Before releasing, run this checklist on Windows, Linux X11 and Wayland, and
macOS; CI compile success and software Vulkan do not establish hardware parity.

- Start new cities at each map size, toggle terrain, and load an existing save.
- Switch both projections and all overlays; inspect buildings, roads, services,
  districts, legends, HUD text, tooltips, and placement warnings.
- Exercise every keyboard shortcut, key repeat, mouse button, drag, pan, and
  cursor-anchored scroll zoom. Move between displays with different scaling.
- Pause, step, change speed, save/load, and test every unsaved-session quit action.
- Repeatedly resize, minimize/restore, close while minimized, and lose focus
  during tool/camera dragging. Restoration must not trigger a burst of ticks.
- Require no validation errors and stable memory after warm-up. Compare Release
  frame times on identical cities against the prior visualizer, excluding capture.

## Troubleshooting

Missing-loader or missing-device errors normally indicate absent Vulkan drivers
or a misplaced MoltenVK manifest. Check `VK_DRIVER_FILES` overrides before
changing the application. Debug validation messages are printed to stderr.
Initialization and device-loss failures exit with a nonzero status; they never
restart or mutate the saved simulation. Unsupported capture formats report a
clear smoke-test error rather than producing corrupt output.

UNORM surface formats are preferred. On sRGB-only surfaces, shader colors and
clear colors are converted to linear values; translucent blending then follows
the surface's linear-light behavior, which can differ from the old encoded-color
blending. Compare those surfaces separately when reviewing visual parity.

## Verification record — October 11, 2026

- macOS Intel/Metal via MoltenVK: strict Debug and Release builds; 374/374
  tests passed, and 372/372 passed with the visualizer disabled. Configuration
  also succeeded with graphics package discovery explicitly disabled.
- Installed macOS launch passed without driver overrides; the executable uses
  bundled relative-path GLFW/Vulkan libraries and the bundled MoltenVK manifest.
- Linux Ubuntu 24.04/Mesa software Vulkan: strict build, X11 and nested Wayland
  captures, installed launch, and the control smoke passed. Loader diagnostics
  confirmed Khronos validation was loaded; no validation errors were reported.
- Linux passed 373/374 unit tests. The existing
  `CitySimulatorTests.RailLinesAppearAlongsideBusesInALargeCity` fails because
  the seeded simulation produces zero rail routes. It also fails with the
  visualizer disabled; simulation behavior and that test were left unchanged.
- Controlled SDL references used the original drawing/HUD code with matching
  seeded scenes, animation time, and Retina scaling. Top-down frames matched
  exactly; isometric differences remained below 0.04% of pixels, within the
  original three-channel-value/1% comparison tolerance.
- A 240-frame Release isometric benchmark on Intel Iris Plus 655 measured
  median CPU drawing preparation at approximately 1.1 ms for Vulkan versus
  6.1 ms for SDL. Both presented at approximately 16.7 ms/frame with vsync.
  This is a single-scene measurement, not a general performance guarantee.
- Windows/NVIDIA GeForce GTX 1660 Ti: strict MSVC Debug build with Vulkan SDK
  1.4.313.0; 373/374 unit tests passed. The same rail-route simulation test
  documented above failed with zero rail routes. The 20-frame graphics smoke
  and all 16 capture color/order checks passed, with Khronos validation and
  synchronization validation enabled and no validation errors.
- The Windows SDK uses a copy-only installation with user-level SDK/PATH
  settings; validation layers are discovered through `VK_ADD_LAYER_PATH` (or
  process-local `VK_LAYER_PATH`). The clean Windows smoke used an empty
  process-local `VK_IMPLICIT_LAYER_PATH` to bypass stale TikTok/Epic overlay
  manifests; the unmodified environment reports loader errors for those
  missing files. No overlay registry entries were changed.
- Windows CI execution, installed-package launch, and the full interactive
  hardware checklist remain pending; all-platform hardware acceptance is
  still a release gate. Local macOS validation-layer compilation was limited
  by disk space, so validation-layer checks were performed on Linux and Windows.
