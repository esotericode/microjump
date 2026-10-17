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
- **Lantern Isles:** an isles capture, the long-leap replay, and a scripted trip
  through both gates.
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

## Character

`character_tests.cpp` plays the committed move tours through the animator and checks
every body part at three blend factors per tick. Each part must be finite, non-mirrored
and near the collision capsule. The run, long-jump dive, wall-kick twirl, squash,
stretch, dust and clean-hop flash must all appear. The tests also check that landings
squash in order of fall height and settle, and that respawns snap instead of
interpolating. See [character](character.md).

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

## The Lantern Isles and gates

`lantern_isles_tests.cpp` checks the shipped isles the way a player meets them:

- every checkpoint settles;
- every spark but the mid-leap one sits over a walkable landing;
- every gate in both levels leads to a real level and checkpoint, and you never
  arrive inside the gate back;
- the Three Steps need exactly one, two and three chained jumps: a search over takeoff
  points shows each step reachable with that chain and never with one jump fewer;
- each secret and expert line can be reached with the moves it claims to need: the gap
  to Echo Plateau with a jump and a dive (a plain jump lands on the catch islet, whose
  mushroom throws you back), the grotto, the Sky Blossom, Hermit's Rock and the
  Skipping Stones;
- nothing below the routes catches a fall.

A small route driver plays the stepping stones, the grove's canopies, both slab bridges,
the ruins' columns and the cloud steps. It runs and judges each takeoff as a player
does: it jumps once its speed would carry it to the landing, or at an edge, and lines
up the touchdown in the air. A wall-kick driver climbs the spire onto its tower. The
long-leap test shows a long jump clears the pier's gap while a running jump falls
short. Any respawn fails a route. Setting `k_route_log` to true in the test prints each
tick of the traced routes, which helps when a new route won't pass.

`jump_chain_tests.cpp` covers the moves themselves:
- the double and triple jump heights, timing window and speed rule;
- that double jumps can be cut short and triple jumps can't;
- the dive's boost and cap, where it can and can't start, belly slides, standing up,
  rollouts and buffered rollouts;
- bounce floors.

`replay_sm64_moves` plays `tests/replays/sm64-moves.mjr` through the whole game.

`collision_tests.cpp` checks that a V of two 60° slopes holds a falling capsule, while a
wall corner does not. `level_tests.cpp` covers the new shapes, `solid`, `title`,
`atmosphere` and gate parsing. It also checks that stepping into a gate is reported
once, never on arrival, and that a hunt survives the trip.

With `MJ_SMOKE_TESTS=ON`:

- `render_isles` and `isles_pixels` capture the isles.
- `travel_gates` runs `--travel-test`, out through the playground's gate and back, so
  both mesh swaps run under validation.
- `replay_isles_leap` replays `tests/replays/isles-long-leap.mjr`, from spawn down the
  pier and over the gap, through the whole game.

## Moving platforms and the Clockwork Garden

`mover_tests.cpp` covers the engine and the rules of riding:
- a scene finds bodies at their poses and turns;
- eased paths, pauses, loops, upright and flat orbits, spins and phases;
- strict mover data, including the 20 m/s limit;
- a slab carries its rider through a whole cycle;
- a lift raises and lowers its rider without losing the ground;
- a turntable carries its rider round and turns them;
- a hop in place lands in place, with no phantom speed;
- jumping off keeps the platform's velocity;
- a mover pushes a standing player without passing through;
- the same input gives the same result.

`clockwork_garden_tests.cpp` uses the shared route driver in `tests/route_driver.hpp`,
which can also wait for a platform, board it, ride it and hop off. It checks:
- every checkpoint stands still, off any mover;
- the lawn catches every fall;
- every spark is reached by its route: the gear stair; the ferry; the gallery lift; the
  dial lift and the minute hand; the gallery's spring; the pistons; the wheel, its
  sky walk and ferry; the carousel's plinths and rim; and the fountain;
- a triple jump the length of the dial reaches the weathervane, and no double jump
  does;
- dropping off the gallery's north edge lands behind the clock.

`render_garden` and `garden_pixels` capture the garden mid-motion under validation.

## Z-fighting

`surface_tests.cpp` builds every shipped level shape by shape and looks for visible
z-fighting. That means front faces of two differently coloured shapes in the same plane,
facing the same way and overlapping, where nothing solid covers the overlap. A small
fixture checks that the detector finds a real overlap and ignores buried and identical
faces. Each mover's shapes are also checked against each other, since they move together.

## Particle rendering

With `MJ_SMOKE_TESTS=ON`, `particle_render` draws a small controlled Vulkan scene and
compares its captures under `tests/captures/particles/`. It checks opacity against the
background, radial edge falloff, opaque-world occlusion, consistent ordering when
inputs are reversed, coincident layers without depth writes, and opaque PNG alpha.
Shutdown validation errors fail the fixture too. CPU tests cover camera-depth sorting,
invalid values, the submission budget, and the character emitter's interpolated birth
and death intervals. See [particles](particles.md) for renderer and timing details.

## Audio

`audio_tests.cpp` covers:
- **The synthesizer:** pitch, silent ends, deterministic noise, and loop seams.
- **The mixer:** silence, centre and side panning, distance cut-off, pitch, voice
  stealing, click-free loops fading in and out, the limiter, and the master volume.
- **The sound bank:** every cue is finite, audible, under full scale and silent at its
  ends. Each one is also written as a WAV to `build/<preset>/tests/sounds/` so a person
  can listen.
- **The director:** it hears steps, jumps, double and triple jumps, landings, rising spark
  chimes, the finished hunt and checkpoints, and stays quiet after a reset. Gears
  tick at their own positions, lifts clunk at each stop and start, and music boxes loop.
- **Launch options:** only play makes sound; scripted checks and captures stay silent.
- **The output:** SDL's dummy device runs the mixer on the audio thread.

`replay_with_sound` replays `sm64-moves.mjr` through the whole game on the dummy device.
It checks that sound starts and that the replay ends exactly where it does in silence.

## Saved expeditions

`progress_tests.cpp` covers strict bounded JSON, duplicate/unknown fields, invalid
snapshots, reordered and removed checkpoint/spark identities, multiple places, missing
and corrupt files, Unicode paths, atomic overwrite and failed-replacement preservation.
`progress_options_tests.cpp` checks explicit profiles, no-save precedence and isolation
for recordings, replays, captures, bounded runs and diagnostic scripts.

With smoke tests enabled, `progress_runtime` launches the actual game repeatedly with a
disposable explicit profile. A fixed-step driver collects a light and visits a checkpoint
in two tiny connected places, crosses a gate naturally and quits. Zero-tick inspections
then prove restoration before anything can be recollected, including after reordering
authored arrays. It also checks packaged last-world resume, missing-world fallback,
byte-for-byte profile isolation, the shared F2 reset handler and corrupt-file recovery.
Every process checks a clean Vulkan shutdown. The fixture never opens the user's profile.

The internal `--progress-test collect|inspect|reset` driver requires `--save PATH`;
collect additionally requires `--level PATH`. It rejects other scripted or bounded
modes, and runs independently of window focus. `collect` advances exactly one 120 Hz
tick per frame for 240 total ticks across travel. Inspect/reset render without advancing
gameplay. The fixture logs both the initial restored state and the final state.
