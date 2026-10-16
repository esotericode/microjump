# microjump

A third-person 3D platformer running on its own engine, written in C++23 with Vulkan.
AI agents build it together: [AGENTS.md](AGENTS.md) guides them, and
[agentcollab.md](agentcollab.md) is where they talk.

![Platform routes seen from the field's finish deck](docs/captures/m4-field.png)

**What's in it today:**

- **A 230 m playground:** a plaza, a sprint lane, 40 route platforms, wall-kick
  towers, a summit stair course, a lighting pavilion and 12 checkpoints.
- **Movement:** running, skids, variable jumps, wall kicks, long jumps, and momentum
  you build on purpose.
- **A renderer:** HDR forward rendering with sun shadows, 16 local lights, GGX
  materials and a day/night cycle.
- **Input:** keyboard/mouse or gamepad, with live tuning of every movement, camera
  and lighting value.
- **Diagnostics:** deterministic input record and replay, GPU frame capture, and
  frame-pacing tools.

## Build and run

### Linux (Ubuntu 26.04)

```sh
sudo apt-get install -y g++ cmake ninja-build clang libclang-rt-21-dev llvm-21 clang-format clang-tidy glslc libvulkan-dev vulkan-validationlayers vulkan-tools mesa-utils pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxrender-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-x11-dev libdecor-0-dev xvfb xauth xdotool
cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON
cmake --build --preset linux-debug
ctest --preset linux-debug
./build/linux-debug/game/microjump
```

The presets are `linux-debug` (GCC with ASan/UBSan and Vulkan validation),
`linux-release` and `linux-clang-debug`.

**Without sudo:** `tools/bootstrap-local.sh` extracts the same packages into
`.local-tools/`. Then put `tools/local-env.sh` in front of every command, for example
`tools/local-env.sh ctest --preset linux-debug`. The wrapper also sets the leak-check
suppressions for the system Wayland decoration plugin (see [testing](docs/testing.md)).

### Windows

Install Visual Studio's C++ x64 tools, CMake 3.28+, Ninja and the
[Vulkan SDK](https://vulkan.lunarg.com/sdk/home). Then, in an x64 Native Tools prompt:

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug
build\windows-debug\game\microjump.exe
```

CI builds every PR and push with GCC, Clang and MSVC. It uploads ready-to-run Windows
and Linux Release zips containing the executable, shaders, levels, SDL and the licence
notices. A Vulkan 1.3 GPU driver is all a player needs.

## Controls

| Keyboard / mouse | Gamepad | Action |
| --- | --- | --- |
| W A S D | Left stick | Move (camera-relative); a partial tilt walks |
| Space | South button | Jump (hold for height); press again at a purple wall to kick |
| Shift + Space | Left shoulder + jump | Long jump while running |
| Ctrl | Right shoulder | Walk |
| Mouse | Right stick | Orbit the camera |
| — | Right-stick click | Recenter the camera |
| R / Home | Back | Respawn at the last checkpoint |
| F1 | North button | Diagnostics overlay |
| F2 | Start | Tuning window (movement, camera, lighting, gamepad, diagnostics) |
| F3 | — | Free-flight camera |
| Escape | — | Quit |

While F2 is open, a controller drives the menu and the keyboard still plays.
Tuning values reset when the game restarts.

## Command line

| Option | Effect |
| --- | --- |
| `--level PATH` | Load a JSON level (default: `assets/levels/playground.json` beside the executable). |
| `--record PATH` / `--replay PATH` | Save every tick's input when the game exits, or play a recording back and exit. |
| `--time-of-day H` | Start at H hours (0–24). |
| `--shadow-quality low\|medium\|high` | Sun shadow preset. |
| `--capture-frame N` / `--capture-tick N` with `--capture-out PATH.png` | Save a GPU frame (rendered frame N, or the exact state at simulation tick N) and exit. |
| `--hide-overlay` | Start with the overlay hidden. |
| `--frames N` / `--smoke-test` | Render N frames (or 12) and exit. |
| `--frame-log PATH` / `--pacing-test` | Per-frame timing CSV; scripted frame-pacing test. |
| `--camera-test` / `--resize-test` | Scripted rendering fixtures used by the tests. |

## Checks

```sh
ctest --preset linux-debug     # unit, simulation, replay and (with MJ_SMOKE_TESTS) GPU tests
find engine game tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror
find engine/src game -name '*.cpp' -print0 | xargs -0 -n 1 clang-tidy -p build/linux-clang-debug
```

## Where things are

| Path | Contents |
| --- | --- |
| `engine/` | The engine library: platform, math, rendering, collision, level loading |
| `game/` | The platformer: player, camera, simulation, levels, debug UI |
| `shaders/` | GLSL, compiled to SPIR-V at build time |
| `assets/levels/` | JSON levels |
| `tests/` | Unit, simulation, replay and GPU tests, plus committed recordings |
| `tools/` | Local toolchain setup and test helpers |
| `docs/` | [Engine](docs/engine.md), [movement](docs/movement.md), [levels](docs/levels.md), [testing](docs/testing.md), [history](docs/history/) |

Third-party libraries and their licences are listed in
[third_party/README.md](third_party/README.md). microjump is [MIT licensed](LICENSE).
