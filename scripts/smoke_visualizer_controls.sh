#!/usr/bin/env bash
# Run under xvfb-run, with xdotool, wmctrl, and Openbox installed. Uses only a temporary save directory.
set -Eeuo pipefail
trap 'echo "Control smoke failed at line $LINENO: $BASH_COMMAND" >&2' ERR
visualizer=$(realpath "$1")
artifacts_dir="${URBAN_SMOKE_ARTIFACTS_DIR:-$PWD/graphics-controls}"
work_dir=$(mktemp -d)
app_pid=''
wm_pid=''
cleanup() {
  status=$?
  if test "$status" -ne 0; then
    cat "$work_dir/visualizer.log" "$work_dir/wm.log" 2>/dev/null || true
    if test -n "${window:-}"; then
      xdotool getwindowname "$window" || true
      xprop -id "$window" _NET_WM_NAME WM_STATE || true
      xprop -root _NET_ACTIVE_WINDOW || true
    fi
    mkdir -p "$artifacts_dir"
    cp -R "$work_dir/." "$artifacts_dir/"
  fi
  test -z "$app_pid" || kill "$app_pid" 2>/dev/null || true
  test -z "$wm_pid" || kill "$wm_pid" 2>/dev/null || true
  rm -rf "$work_dir"
}
trap cleanup EXIT
cd "$work_dir"
cat > saved_state.py <<'PYHELPER'
import json
import os
import time
from pathlib import Path

def read_saved_state():
    session_path = Path('urban_sim_session.json')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            if session_path.stat().st_mtime_ns >= int(os.environ['URBAN_SMOKE_SAVE_AFTER_NS']):
                session = json.loads(session_path.read_text())
                city = json.loads(Path(session['citySnapshot']).read_text())
                return session, city
        except (FileNotFoundError, json.JSONDecodeError):
            pass
        time.sleep(0.05)
    raise AssertionError('Timed out waiting for completed session and snapshot files')
PYHELPER
save_checkpoint() {
  export URBAN_SMOKE_SAVE_AFTER_NS=$(python3 -c 'import time; print(time.time_ns())')
  tap F5
}

openbox --sm-disable > wm.log 2>&1 &
wm_pid=$!
for attempt in $(seq 1 100); do
  if xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q '0x'; then break; fi
  sleep 0.1
done
"$visualizer" > visualizer.log 2>&1 &
app_pid=$!
window=''
for attempt in $(seq 1 100); do
  window=$(xdotool search --onlyvisible --name 'UrbanSimCore Visualizer' 2>/dev/null | head -1 || true)
  test -z "$window" || break
  sleep 0.1
done
if test -z "$window"; then cat visualizer.log; exit 1; fi
xdotool windowactivate --sync "$window"
xdotool windowfocus --sync "$window"
tap() { xdotool key --clearmodifiers "$1"; sleep 0.15; }
drag() {
  xdotool mousemove --window "$window" "$1" "$2" mousedown 1
  sleep 0.15
  xdotool mousemove --window "$window" "$3" "$4"
  sleep 0.15
  xdotool mouseup 1
  sleep 0.2
}
# Mapping the GLFW window precedes Vulkan initialization. Wait for a
# successfully presented start frame before sending native keyboard input.
for attempt in $(seq 1 200); do
  if xdotool getwindowname "$window" | grep -q '(Start Screen)'; then break; fi
  kill -0 "$app_pid"
  sleep 0.1
done
xdotool getwindowname "$window" | grep -q '(Start Screen)'
tap Return
for attempt in $(seq 1 100); do
  if xdotool getwindowname "$window" | grep -q ' | '; then break; fi
  sleep 0.1
done
xdotool getwindowname "$window" | grep -q ' | '

tap F3 # top-down, 28 logical pixels/tile
tap F4 # compact UI
tap r
drag 126 350 294 350 # road along row 12
tap z
drag 126 322 294 322 # residential zoning alongside the road
tap period
save_checkpoint
python3 - <<'PY'
import json
from pathlib import Path
from saved_state import read_saved_state
session, city = read_saved_state()
assert session['tick'] == 1 and session['paused'], session
assert session['funds'] < 50000, 'Input tools did not spend funds'
assert len(city['roads']) >= 6, 'Road drag was not applied'
assert sum(tile['zone'] == 1 for tile in city['tiles']) >= 7, 'Zoning drag was not applied'
Path('before.json').write_text(json.dumps({'funds': session['funds'], 'roads': len(city['roads'])}))
PY
# A focus loss while holding the mouse must cancel the pending road placement.
tap r
xdotool mousemove --window "$window" 406 434 mousedown 1
xdotool mousemove --window "$window" 574 434
sleep 0.15
xdotool windowminimize "$window"
sleep 0.3
xdotool windowmap "$window" windowactivate --sync "$window"
sleep 0.2
xdotool mouseup 1
xdotool windowsize --sync "$window" 1100 720
sleep 0.3
xdotool windowsize --sync "$window" 1280 800
sleep 0.3
save_checkpoint
python3 - <<'PY'
import json
from pathlib import Path
from saved_state import read_saved_state
session, city = read_saved_state()
before = json.loads(Path('before.json').read_text())
assert session['funds'] == before['funds'], 'Focus loss committed an unfinished tool'
assert len(city['roads']) == before['roads'], 'Focus loss committed an unfinished road'
PY
for key in 1 2 3 4 5 6 7 8 F2 F3 F3 F9; do tap "$key"; done
# A native close request while minimized must remain responsive and preserve the dialog.
xdotool windowminimize "$window"
sleep 0.2
wmctrl -ic "$window"
sleep 0.2
kill -0 "$app_pid"
xdotool windowmap "$window" windowactivate --sync "$window"
sleep 0.2
tap Escape # cancel the pending quit dialog
kill -0 "$app_pid"
tap Escape # reopen the dialog
xdotool mousemove --window "$window" 640 467 click 1 # Quit without saving
for attempt in $(seq 1 100); do
  kill -0 "$app_pid" 2>/dev/null || break
  sleep 0.1
done
if kill -0 "$app_pid" 2>/dev/null; then cat visualizer.log; exit 1; fi
wait "$app_pid"
app_pid=''
if grep -q 'Validation Error' visualizer.log; then cat visualizer.log; exit 1; fi
echo 'Visualizer controls: road/zoning drags, step, save/load, overlays, focus loss, minimize/restore, resize, close while minimized, and quit/cancel passed'
