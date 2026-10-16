# Testing and diagnostics

## Running the tests

```sh
cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON   # also: linux-release, linux-clang-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
# A CI-like run with software Vulkan and no desktop:
env SDL_VIDEO_DRIVER=x11 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
    xvfb-run -a ctest --preset linux-clang-debug
```

Without sudo, put `tools/local-env.sh` in front of each command.

**Debug builds:**

- They run ASan and UBSan, and validation layers with synchronization validation.
- Any Vulkan validation error makes the run exit with an error.
- CI treats warnings as errors with GCC, Clang and MSVC.
- CI also checks `clang-format` and `clang-tidy`. The commands are in the README.

## What the tests cover

`microjump_tests` (doctest) holds the headless tests: math, meshes, the command line,
collision queries and the mover, movement verbs and momentum, the camera, level
parsing, the replay format, lighting math, frame-pacing statistics and SDL
virtual-gamepad input. Two things are checked across render cadences:

- **Determinism:** the same input fed through 30, 60, 144 and 240 Hz, and an irregular
  4–31 ms frame schedule, gives bit-identical player and camera state.
- **Round trip:** recordings are serialized, reloaded and replayed, and must match bit
  for bit.

With `MJ_SMOKE_TESTS=ON`, CTest also runs the game itself:

- **Captures:** startup, render, camera, resize and replay captures.
- **Lighting:** day, night and pavilion captures, decoded and pixel-checked by
  `tests/capture_check.cpp`.
- **Playground:** a playground capture.
- **Gamepad:** the full game window driven by a virtual gamepad, including the tuning
  menu.

Replay checks (`check_replay.cmake`) require the expected final state, zero validation
errors and a clean exit code, so a crash at shutdown can't hide behind passing output.

### Committed recordings (`tests/replays/`)

| File | Level | Source | Expected result |
| --- | --- | --- | --- |
| `m2-input-smoke.mjr` | movement yard | Real X11 input | Tick 439, grounded |
| `m3-pavilion-tour.mjr` | movement yard | Scripted waypoints | Pavilion capture at tick 1380 |
| `m4-moves-tour.mjr` | playground | Scripted | Long jump, three wall kicks, tick 2899 on checkpoint 10 |
| `m4-momentum-chain.mjr` | playground | Scripted | Four clean long jumps up to the 22 m/s cap |
| `empty.mjr`, `overflow.mjr` | — | Edge cases | Finishes at tick 0 / is rejected |

If a gameplay change alters movement, the scripted recordings may need to be
regenerated. Say so in the PR. `tools/input-smoke.sh`, run inside Xvfb, drives the game
with real X11 key and mouse events while recording, so the result can be replayed and
compared.

## Captures

Use the engine's own capture, not desktop screenshots. GNOME on Wayland blocks
generic screen capture.

```sh
./build/linux-debug/game/microjump --hide-overlay --capture-tick 600 --capture-out shot.png
./build/linux-debug/game/microjump --replay tests/replays/m4-moves-tour.mjr \
    --capture-tick 2648 --capture-out kick.png
```

**Frame or tick:**

- `--capture-tick` shows the exact simulation state at that tick, so it is reproducible
  at any frame rate.
- `--capture-frame` counts rendered frames instead.

PNGs come from the GPU at the window's real pixel size. Showing before/after captures in
a PR is the clearest way to present visual changes.

## Frame pacing

- **The overlay** (F1) graphs frame intervals and shows mean, jitter, long frames and
  frames whose tick count changed.
- **`--frame-log PATH`** writes one CSV row per frame with these columns:
  - intervals, ticks and interpolation alpha;
  - simulation time;
  - the blocking points: display wait, rebuild, frame slot, acquire, record and present;
  - GPU time, the swapchain image and mouse deltas;
  - events such as `shadow_rebuilt_High` and `phase_N`.
- **`--pacing-test`** runs nine 5-second phases and prints a steady-state summary for
  each:
  1. baseline;
  2. shadows High;
  3. shadows Low;
  4. shadows Medium;
  5. GPU idle wait;
  6. shadow map rebuild;
  7. swapchain recreation;
  8. refresh-snapped frame time;
  9. display pacing off with a GPU idle wait.

On Windows, run `microjump.exe --pacing-test --frame-log pacing.csv > pacing.txt 2>&1`
with the window focused. PowerShell writes `pacing.txt` as UTF-16.

## Sanitizer suppressions

`tests/lsan.supp` suppresses two leaks inside the system's Wayland decoration plugin,
not in our code:

- Fontconfig caches that libdecor's GTK plugin keeps after SDL shuts down;
- a 96-byte `wl_proxy` that `libdecor-gtk` sometimes leaks in `libdecor_frame_commit`.

Everything else stays checked. Sanitized builds link SDL's X11 backend directly
instead of loading it at runtime, so Xlib's caches stay visible to LSan instead of being
reported as leaks.

## Known test quirks

- Once, in about 24 native Wayland runs, `--resize-test` captured the frame after the
  first resize instead of the second, probably because of late resize
  acknowledgement. Xvfb resizes synchronously, so CI doesn't hit it.
- Replays are exact within one build. Committed-replay tests allow tiny tolerances for
  other compilers.

## Spark hunt

The collectible tests cover strict optional JSON data, capsule and swept pickups, wall
occlusion, respawn retention, and collection determinism across render cadences. They
also compare movement bit for bit with the same level without collectibles. Content
tests settle the player at every authored spark and exercise routes to the rewards.

`tests/replays/spark-invitation.mjr` walks from spawn through **First light**. With smoke
tests enabled, `render_spark_pickup` captures tick 140 with the counter at 1/16 and
`replay_spark_invitation` checks the completed replay at tick 170. Both require zero
Vulkan validation errors. F1 can close diagnostics while leaving the hunt HUD visible;
`--hide-overlay` suppresses both for clean scene captures.
