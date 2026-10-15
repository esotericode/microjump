#!/usr/bin/env bash
# Run inside Xvfb. Uses real X11 events rather than the camera test's scripted input.
set -euo pipefail
task_executable="${1:?Pass the built microjump executable}"
task_output="${2:?Pass a PNG output path}"
"$task_executable" --capture-frame 240 --capture-out "$task_output" &
task_pid=$!
trap 'kill "$task_pid" 2>/dev/null || true; wait "$task_pid" 2>/dev/null || true' EXIT
task_window="$(timeout 10s xdotool search --sync --onlyvisible --pid "$task_pid" --name '^microjump$' | head -n 1)"
xdotool windowfocus --sync "$task_window"
sleep 0.25
xdotool keydown w
sleep 0.4
xdotool keyup w
xdotool mousedown 3
sleep 0.1
xdotool mousemove_relative -- 70 -12
sleep 0.15
xdotool mouseup 3
xdotool key F1
wait "$task_pid"
trap - EXIT
