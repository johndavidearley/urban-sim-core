#!/usr/bin/env bash
# Invoke under xvfb-run so nested Weston supplies a real input seat to GLFW 3.4.
set -euo pipefail
visualizer=$(realpath "$1")
captures=$(realpath -m "$2")
script_dir=$(cd "$(dirname "$0")" && pwd)
export XDG_RUNTIME_DIR="$(mktemp -d)"
chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=x11-backend.so --renderer=pixman --socket=urban-smoke --idle-time=0 > weston.log 2>&1 &
compositor_pid=$!
cleanup() {
  kill "$compositor_pid" 2>/dev/null || true
  wait "$compositor_pid" 2>/dev/null || true
  rmdir "$XDG_RUNTIME_DIR" 2>/dev/null || true
}
trap cleanup EXIT
for attempt in $(seq 1 50); do
  test -S "$XDG_RUNTIME_DIR/urban-smoke" && break
  sleep 0.1
done
test -S "$XDG_RUNTIME_DIR/urban-smoke" || { cat weston.log; exit 1; }
env -u DISPLAY WAYLAND_DISPLAY=urban-smoke timeout 90s "$visualizer" --graphics-smoke "$captures"
python3 "$script_dir/check_graphics_capture.py" "$captures"
