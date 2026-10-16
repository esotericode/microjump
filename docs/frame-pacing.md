# Frame-pacing diagnostics

2026-10-16. Owner report: on the **Windows** build, changing the shadow quality in F2
leaves the game no longer locked smoothly to 60 FPS until restart. It is not a general
performance drop. It was not reproduced on the Linux development laptop.

## What the code does on a shadow change

The renderer calls `vkDeviceWaitIdle` and creates a new shadow map plus its descriptor
writes. Nothing else changes: the swapchain, present mode (FIFO), frame slots and
timeline values all continue. So the lasting effect must come from somewhere outside
that code path. Candidates:

1. **Missed vblanks.** Frames are shown twice. This would appear as ~33 ms intervals
   and long `acquire`/`present` blocking.
2. **Timer jitter.** After the stall, the CPU settles into blocking at a different
   point (frame wait, acquire or present). Some blocking points wake less regularly
   on Windows, and the simulation clock and interpolation follow the CPU timestamps.
   This would appear as a larger interval deviation with no 33 ms frames.
3. **Mouse delivery.** F2 releases relative mouse mode and closing it captures again.
   If capture returns in a different mode, look deltas arrive unevenly even at a
   perfect 60 FPS. This would appear as irregular `mouse_dx` between frames.
4. **Presentation mode.** Windows' compositor stops presenting the window directly.
   This usually adds latency and changes the blocking pattern.

The diagnostics below tell these apart without guessing.

## Tools

- **F1 overlay:**
  - a frame-interval graph covering the last 240 frames;
  - mean, jitter (standard deviation), range, *long frames* (over 1.5× the mean) and
    *uneven tick frames* (frames whose 120 Hz tick count changed).
- **F2 → Diagnostics: frame pacing:**
  - the display refresh rate;
  - **Snap frame time to refresh** (an experiment, off by default);
  - one-shot buttons: **Wait for GPU idle**, **Rebuild shadow map** (same quality) and
    **Recreate swapchain**;
  - **Start/Stop frame log**, which writes `frame-log.csv` beside the executable.
- **`--frame-log PATH`:** a CSV with one row per frame. Columns: interval, interval
  fed to the simulation, ticks, interpolation alpha, simulation CPU time, rebuild
  stall, then waits in frame slot / acquire / record / present. After those come GPU
  time from timestamp queries, swapchain image and mouse deltas. Events include
  `shadow_rebuilt_High`, `swapchain_rebuilt`, `tuning_open`, `mouse_captured`,
  `focus_lost`, `action_*` and `phase_N`.
- **`--pacing-test`:** a scripted run of 8 phases of 5 s each:
  1. baseline;
  2. shadows High;
  3. shadows Low;
  4. shadows Medium;
  5. GPU idle wait;
  6. shadow map rebuilt at the same quality;
  7. swapchain recreated;
  8. frame time snapped to the refresh rate.

  It prints one steady-state summary per phase (the last 4 s, after the rebuild
  hitch) and exits.
- **Startup log:** shows the display refresh rate, swapchain image count, and whether
  GPU timestamps are available.

## Local result (Linux, Intel Meteor Lake, GNOME Wayland, Debug build)

Zero validation errors. Steady state was unchanged across all phases: means of
16.67–16.78 ms, with the CPU blocked about 15.2 ms in acquire in every phase. The
GPU took 4 ms at Medium and 8 ms at High. Occasional single 30 ms compositor
hiccups appeared at random times, unrelated to the rebuilds, and none persisted.
The problem does not reproduce here, which matches the owner's observation.

## Windows result and diagnosis

The owner ran `--pacing-test` and a manual session on an NVIDIA GeForce RTX 4070
SUPER (60 Hz, 1280×720 window, Release build). The logs were read and then removed
from the repository.

**Manual session:**

| Period | Image sequence | Interval jitter |
| --- | --- | --- |
| Start, and while F2 was open | `1 0 1 0 …` | 1.0–1.1 ms |
| After shadows → High (19.3 s) | `1 0 0 1 1 0 0 …` (half the frames reuse the previous image) | 13.2 ms |
| Mouse captured or released again | unchanged | 13.1 ms |
| After **Recreate swapchain** (50.3 s) | `1 2 1 2 …` | 0.6 ms |

**Scripted test:** this run started in the broken state. Phases 1–6 (all shadow
changes, the GPU idle wait and the same-quality rebuild) each had about 15 ms of
jitter, and 239 of 240 frames changed their tick count. The swapchain-recreate
phase dropped jitter to 0.3 ms, and it stayed there.

**Mechanism.** In the broken state, `vkAcquireNextImageKHR` returns the image that
was presented on the previous frame. The new frame's GPU work therefore waits until
that image leaves the screen. Frames come in pairs: two frames about 0.2–3 ms apart,
then a wait of about 33 ms on the frame slot. The average is still 16.67 ms, so the
readout says 60 FPS. But each pair samples almost the same game time while the two
frames are displayed one refresh apart, so motion moves at 30 Hz in 60 FPS clothing.

`vkDeviceWaitIdle` during a shadow change triggered the state mid-session (it also
waits on the presentation work queued on the graphics queue). The state can also
appear at startup. Mouse capture and the tuning window had no effect.

## Fix (D-045)

1. **No device-wide wait on shadow changes.**
   - The new shadow map is created immediately.
   - Each frame slot rebinds it after that slot's own previous frame has finished.
   - The old map is retired until the timeline shows that no in-flight frame uses it.
   - A shadow change now costs about 0.1 ms locally, against 5–9 ms with
     `vkDeviceWaitIdle`, and passes validation.
   - Resizes, swapchain recreation and the diagnostic idle button still idle the device.
2. **Automatic recovery.**
   - The renderer checks 32-frame windows for the combined signature: at least 12
     frames reacquired the image just presented, and the intervals split into at
     least 10 short frames (under 0.3× the mean) and 10 long ones (over 1.6× the
     mean).
   - On a match it recreates the swapchain, the same fix as the owner's button.
   - It is logged (`Paired presentation detected …`, CSV event `pacing_recovered`)
     and rate-limited to 3 recreations per 30 s, after which it stops.
   - It can be turned off with F2 → Diagnostics → *Recover from paired presentation*.
   - Unit tests replay 32-frame windows taken from the Windows logs (paired and
     recovered) and from Linux. Either symptom alone does not trigger recovery.
   - On the Linux machine, natively and under Xvfb with software Vulkan, all four
     images rotate (`0 1 2 3`) with zero reuse, and no recovery fires.

Still to confirm on Windows: the scripted test should show no paired phase, and the
*GPU idle wait* phase should recover automatically if that stall re-triggers pairing.

## Original request to the owner (Windows)


From a terminal in the unzipped folder:

```text
microjump.exe --pacing-test --frame-log pacing.csv > pacing.txt 2>&1
```

Keep the window focused and don't touch the controls for the 45 seconds. Then send
`pacing.txt` and `pacing.csv`.

Then reproduce it the way you noticed it, with a log:

```text
microjump.exe --frame-log manual.csv > manual.txt 2>&1
```

1. Play for about 10 s, then press F2 and change Shadow quality.
2. Close F2 and play for another 10 s.
3. If the stutter appears, open F2 → **Diagnostics** and press **Recreate swapchain**,
   then play 10 s. Then tick **Snap frame time to refresh** and play 10 s.
4. Note whether either of those restored smoothness.
5. Send both files, plus the GPU model, monitor refresh rate, and whether the window
   was windowed or maximized.

Also useful: does opening and closing F2 *without* changing anything also trigger it?
Does changing another setting, such as Time of day, trigger it?
