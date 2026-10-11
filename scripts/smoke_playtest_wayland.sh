#!/usr/bin/env bash
# Run under xvfb-run; nested Weston provides the input seat required by GLFW.
set -euo pipefail
export URBAN_SIM_PLAYTEST_EXECUTABLE="$(realpath "$1")"
export URBAN_SIM_PLAYTEST_ARTIFACTS="$(realpath -m "$2")"
export XDG_RUNTIME_DIR="$(mktemp -d)"
chmod 700 "$XDG_RUNTIME_DIR"
mkdir -p "$URBAN_SIM_PLAYTEST_ARTIFACTS"
weston --backend=x11-backend.so --renderer=pixman --socket=urban-playtest --idle-time=0 > "$URBAN_SIM_PLAYTEST_ARTIFACTS/weston.log" 2>&1 &
compositor_pid=$!
cleanup() {
  kill "$compositor_pid" 2>/dev/null || true
  wait "$compositor_pid" 2>/dev/null || true
  rmdir "$XDG_RUNTIME_DIR" 2>/dev/null || true
}
trap cleanup EXIT
for attempt in $(seq 1 50); do
  test -S "$XDG_RUNTIME_DIR/urban-playtest" && break
  sleep 0.1
done
test -S "$XDG_RUNTIME_DIR/urban-playtest" || { cat "$URBAN_SIM_PLAYTEST_ARTIFACTS/weston.log"; exit 1; }
env -u DISPLAY WAYLAND_DISPLAY=urban-playtest python -m unittest discover -s playtest/tests -p test_rendered.py -v
