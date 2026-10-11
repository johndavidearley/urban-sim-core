#!/usr/bin/env bash
# Run under xvfb-run; a window manager is required for native minimize/restore.
set -euo pipefail
mkdir -p "${URBAN_SIM_PLAYTEST_ARTIFACTS:-playtest-artifacts}"
openbox --sm-disable > "${URBAN_SIM_PLAYTEST_ARTIFACTS:-playtest-artifacts}/openbox.log" 2>&1 &
wm_pid=$!
trap 'kill "$wm_pid" 2>/dev/null || true; wait "$wm_pid" 2>/dev/null || true' EXIT
for attempt in $(seq 1 50); do
  if xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q '0x'; then break; fi
  sleep 0.1
done
python -m unittest discover -s playtest/tests -p test_rendered.py -v
