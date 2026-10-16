# microjump

A custom C++23/Vulkan 1.3 engine, proven through an original 3D movement playground.
[AGENTS.md](AGENTS.md) is the authoritative charter and roadmap.

M2 adds a playable movement slice. A capsule character runs, skids, jumps with
variable height, coyote time and buffering, and steers in the air. It moves through a
graybox test yard (slopes, stairs, steps, platforms, walls) with an engine-owned
collide-and-slide controller. A third-person camera orbits with obstruction handling.
Input can be recorded and replayed deterministically, and every movement and camera value
is tunable live ([M2 notes](docs/m2-movement.md)).

M3 adds real-time lighting:

- an HDR forward renderer with tone mapping
- a dynamic sun shadow map with Low/Medium/High quality
- up to 16 moving point and spot lights
- roughness/metallic materials
- a sky, and a day/night cycle where the moon takes over at night
- a lighting pavilion with material samples and orbiting colored lights, plus street lamps
  that switch on at dusk

Details: [M3 notes](docs/m3-lighting.md). M4 now starts with a **230 m JSON playground**:
40 raised route platforms, an extended sprint lane, a summit stair/ramp course,
10 checkpoints and seven named regions. The original yard remains available as a
separate level for regression replays. [M4 notes](docs/m4-playground.md) and the
[level format](docs/level-format.md) describe editing and routes. Wall jumps and the
momentum move remain the next M4 slice; display settings are M5.

![New platform routes from the finish deck](docs/captures/m4-field.png)

## Linux

Ubuntu 26.04; GCC 15 or Clang 21, CMake 3.28+, Ninja and a Vulkan 1.3 driver:

```sh
sudo apt-get install -y g++ cmake ninja-build clang libclang-rt-21-dev llvm-21 clang-format clang-tidy glslc libvulkan-dev vulkan-validationlayers vulkan-tools mesa-utils pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxrender-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-x11-dev libdecor-0-dev xvfb xauth xdotool
cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
./build/linux-debug/game/microjump
```

Use `linux-release` or `linux-clang-debug` for the other configurations. CMake fetches
only [approved, pinned dependencies](third_party/README.md) and builds shaders with
`glslc`. Debug enables ASan, UBSan and mandatory Vulkan synchronization validation.

Without sudo, `tools/bootstrap-local.sh` extracts Ubuntu development packages into
ignored `.local-tools/`; prefix commands with `tools/local-env.sh`:

```sh
tools/bootstrap-local.sh
tools/local-env.sh cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON
tools/local-env.sh cmake --build --preset linux-debug --parallel 4
tools/local-env.sh ctest --preset linux-debug
tools/local-env.sh ./build/linux-debug/game/microjump
```

The wrapper provides local tool/library paths and scoped Fontconfig / libdecor GTK LSan
suppressions for the system Wayland decoration plugin. All other leak checks stay
active. Sanitized builds link SDL's X11 backend directly so Xlib cache roots remain
visible to LSan. See [M0 evidence](docs/m0-evidence.md) and [M1 evidence](docs/m1-evidence.md).

## Controls and capture

| Keyboard / mouse | Gamepad | Action |
| --- | --- | --- |
| W / A / S / D | Left stick | Move (camera-relative) |
| Space | A (south) | Jump; hold for height |
| Ctrl | — | Walk |
| Mouse | Right stick | Orbit the camera (mouse is captured while focused) |
| R / Home | Back | Respawn at the last checkpoint |
| F1 | — | Toggle the diagnostics/movement overlay |
| F2 | Start | Tuning window: lighting (time of day, day/night, shadow quality, biases), movement, camera (frees the mouse) |
| F3 | — | Free-flight camera (WASD/QE, Shift fast, Home reset) |
| Escape / close | — | Exit |

The overlay shows FPS/frame time, pixel resolution, effective VSync and cap state, plus
movement state, speed, position, ground angle, checkpoint, loaded level and region. VSync is FIFO with no
application FPS cap for now; display-setting controls and persistence are M5. The
simulation runs at a fixed 120 Hz with interpolated rendering, so render rate never
changes movement.

```sh
./build/linux-debug/game/microjump --frames 60
./build/linux-debug/game/microjump --capture-frame 12 --capture-out captures/scene.png
# Record every simulation tick's input on exit, then replay it (exits when it ends).
./build/linux-debug/game/microjump --record captures/run.mjr
./build/linux-debug/game/microjump --replay captures/run.mjr
# Load an authored level; old M2/M3 recordings use the original yard.
./build/linux-debug/game/microjump --level assets/levels/movement-yard.json \
    --replay tests/replays/m2-input-smoke.mjr
# Lighting: start time (hours) and sun shadow preset; clean or tick-timed captures.
./build/linux-debug/game/microjump --time-of-day 21 --shadow-quality high
./build/linux-debug/game/microjump --level assets/levels/movement-yard.json \
    --replay tests/replays/m3-pavilion-tour.mjr --time-of-day 22 \
    --hide-overlay --capture-tick 1380 --capture-out captures/pavilion.png
# Correctness fixtures: camera movement and two live resizes before capture.
./build/linux-debug/game/microjump --camera-test --capture-frame 36 --capture-out captures/camera.png
./build/linux-debug/game/microjump --resize-test --capture-frame 16 --capture-out captures/resized.png
```

Frame numbers are one-based rendered frames. `--capture-frame` saves that frame and
exits; use it with `--capture-out`. PNGs come from the GPU color target after scene
and overlay drawing, before the same image is blitted to the presentation surface.
They use actual pixels, so a high-DPI display can produce a larger PNG than the
logical window size. `--capture-tick` captures the exact simulation state with interpolation
disabled, so it is reproducible across render cadences. `--smoke-test` renders 12 frames and exits. Unknown/invalid
arguments fail explicitly. Asset/shader paths resolve relative to the executable.

## Windows

Install Visual Studio's C++ x64 tools, CMake 3.28+, Ninja and the
[Vulkan SDK](https://vulkan.lunarg.com/sdk/home). In an x64 Native Tools prompt:

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 4
ctest --preset windows-debug
build\windows-debug\game\microjump.exe --capture-frame 12 --capture-out frame.png
```

CI builds MSVC Debug/Release and uploads a Windows Release zip: executable, compiled
`shaders/`, JSON levels in `assets/levels/`, SDL3, redistributable MSVC runtime DLLs and license notices. Extract the
entire zip and run `microjump.exe`. A Vulkan 1.3 GPU driver is required; Release does
not need an installed SDK. Hosted Windows CI has no suitable GPU, so hardware rendering
and controls on the owner's PC remain a separate acceptance check linked from the PR.

## Verification and packaging

CTest covers math/mesh/CLI correctness, collision queries and the character mover,
movement behavior (jump apex, acceleration, skid, coyote time, buffering, stairs, slopes,
respawn, camera obstruction), bit-identical simulation at different render cadences,
the replay format and a committed real-input replay. It also covers the lighting math:
inverse, look-at, orthographic projection, texel-snapped shadow fitting, day/night
continuity, and light fixtures. Level tests check schema validation, the legacy recording,
every playground checkpoint, the extended lane and summit stairs. With `MJ_SMOKE_TESTS=ON` it adds GPU
render/camera/resize/replay captures and day/night/pavilion lighting captures, all with
decoded pixel checks. CI uploads those
captures and runtime zips. No benchmarks or FPS targets.

```sh
find engine game tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror
find engine/src game -name '*.cpp' -print0 | xargs -0 -n 1 clang-tidy -p build/linux-clang-debug
cpack --config build/linux-release/CPackConfig.cmake -B build/packages
```

For a real SDL input check, install `xdotool` and run `tools/input-smoke.sh` inside
Xvfb. It drives the game with X11 keyboard/mouse events while recording, so the
recording can be replayed and compared; see [docs/m2-movement.md](docs/m2-movement.md).
