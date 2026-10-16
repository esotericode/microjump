#!/usr/bin/env bash
# Run inside Xvfb. Drives the game with real X11 keyboard/mouse events while recording the
# simulation input, so the recording can be replayed and compared (see docs/m2-movement.md).
set -euo pipefail
task_executable="${1:?Pass the built microjump executable}"
task_output="${2:?Pass a PNG output path}"
task_record="${3:?Pass a replay output path (.mjr)}"
"$task_executable" --record "$task_record" --capture-frame 420 --capture-out "$task_output" &
task_pid=$!
trap 'kill "$task_pid" 2>/dev/null || true; wait "$task_pid" 2>/dev/null || true' EXIT
task_window="$(timeout 10s xdotool search --sync --onlyvisible --pid "$task_pid" --name '^microjump$' | head -n 1)"
xdotool windowfocus --sync "$task_window"
sleep 0.5
# Run forward, turn, tap jump, use Shift+Space for a long jump, strafe, toggle the overlay.
xdotool keydown w
sleep 0.6
xdotool mousemove_relative -- 160 0
sleep 0.3
xdotool key space
sleep 0.5
xdotool keydown Shift_L
xdotool keydown space
sleep 0.5
xdotool keyup space
xdotool keyup Shift_L
xdotool keydown d
sleep 0.4
xdotool keyup d
xdotool mousemove_relative -- -80 -20
sleep 0.4
xdotool keyup w
xdotool key F1
wait "$task_pid"
trap - EXIT
