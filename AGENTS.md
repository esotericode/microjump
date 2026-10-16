# microjump — Project Charter and Agent Guide
## A custom C++ game engine, proven through a 3D platformer

**Document version:** 0.3
**Status:** M4 wall/long-jump increment: designated wall kicks, running long jump and reachable finish decks implemented. M2/M3 owner gates remain open. See Section 15 and `docs/m4-playground.md`.
**Authority:** This file is the authoritative project charter and the operating guide for AI programming agents. Read it fully before planning any work.

> **Our north star:** Build a high-quality 3D game engine of our own in modern C++, and demonstrate it with a polished, expressive 3D platformer. The engine is the reason for the project; the game is how we prove it works.

---

## 1. Why this project exists

This is an experiment in AI-led software engineering. We want to learn how effectively LLM programming agents can **design, implement, test, and improve** a real low-level system over time. Success is not merely getting a character on screen. Success means building a coherent, original, understandable engine whose behavior can be demonstrated and verified.

We value careful engineering over shortcuts. A small, excellent engine and one compelling playground are better than a large, fragile game. We will deliberately invest in the engine, even when an off-the-shelf tool could produce a game faster.

### Project priorities, in order

1. **Custom engine ownership and technical learning.** Our architecture and core systems must be ours.
2. **Correctness, stability, and maintainability.** Reliable behavior and code quality come before feature count.
3. **Responsive movement and camera feel.** The prototype should be genuinely enjoyable to control.
4. **Expressive real-time lighting.** Simple geometry should still look attractive and physically convincing enough to communicate shape and space.
5. **Useful display settings and an FPS readout.** The player can see the frame rate and change resolution, FPS cap, and VSync. (See Section 11.)
6. **Scope discipline.** Finish each meaningful slice before expanding the project.

---

## 2. Agent quick reference

### Commands

> Working development commands (system tools). For tools extracted without sudo, prefix each command with `tools/local-env.sh`; see `README.md`.

```sh
cmake --preset linux-debug            # configure (also: linux-release)
cmake --build --preset linux-debug    # build
ctest --preset linux-debug            # run all automated tests
./build/linux-debug/game/microjump    # movement yard: WASD/Space/mouse, F1 overlay, F2 tuning
./build/linux-debug/game/microjump --capture-frame 12 --capture-out captures/scene.png
./build/linux-debug/game/microjump --level assets/levels/movement-yard.json --replay tests/replays/m2-input-smoke.mjr
# Add -DMJ_SMOKE_TESTS=ON at configure time to include GPU/capture tests in CTest.
```

Runtime flags (all available):

| Flag | Purpose | Milestone |
| --- | --- | --- |
| `--smoke-test` | Render 12 frames and exit; validation errors fail. | M0/M1 |
| `--capture-frame N --capture-out PATH.png` | Render N frames, save the frame from the GPU as PNG, exit. | M1 |
| `--frames N` | Render N frames then exit cleanly. | M1 |
| `--camera-test` / `--resize-test` | Scripted render correctness fixtures (not input replay). | M1 |
| `--record PATH` / `--replay PATH` | Record every tick's input on exit / replay it instead of live input, then exit. Format: `docs/m2-movement.md`. | M2 |
| `--time-of-day H` / `--shadow-quality low\|medium\|high` | Start time (hours) and sun shadow preset. | M3 |
| `--capture-tick N` / `--hide-overlay` | Capture the exact state at simulation tick N; start with the overlay hidden. | M3 |
| `--level PATH` | Load a version-1 JSON level. Default: packaged `assets/levels/playground.json`; legacy replays use `movement-yard.json`. | M4 |
| `--frame-log PATH` / `--pacing-test` | Per-frame timing CSV; scripted shadow/stall/swapchain sequence with per-phase pacing summaries (`docs/frame-pacing.md`). | Diagnostics |

### Local development environment

Agents run **locally on the project owner's Linux machine** and may use its GPU, build, run the game, run tests, and capture screenshots as needed.

| Item | Value |
| --- | --- |
| OS | Ubuntu 26.04 LTS, GNOME on **Wayland** |
| CPU / RAM | Intel Core Ultra 5 135U / 14 GiB |
| GPU | Intel Meteor Lake integrated graphics, Mesa 26 (ANV Vulkan driver) |
| Display | 3072×1920 @ 60 Hz |
| Compiler present | g++ 15.2 |
| Development tools | Setup command below; this checkout uses ignored `.local-tools/` extraction because sudo requires interactive authentication. |

- Install missing development tools as needed and list every one in the setup command below.
- **Setup command (Ubuntu 26.04):**
  ```sh
  sudo apt-get install -y g++ cmake ninja-build clang libclang-rt-21-dev llvm-21 clang-format clang-tidy glslc libvulkan-dev vulkan-validationlayers vulkan-tools mesa-utils pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxrender-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-x11-dev libdecor-0-dev xvfb xauth xdotool
  ```
  Without sudo: `tools/bootstrap-local.sh`, then prefix development commands with `tools/local-env.sh`.
- **Screenshots:** GNOME/Wayland blocks generic desktop capture. Use the engine's own `--capture-frame` mode; do not rely on desktop screenshot tools.
- Close game windows and stray processes you started when you are done.
- Do not change drivers, power settings, or other system configuration without asking the owner.

### Workflow

- **One agent works at a time** (at least until M2; see D-013).
- Work on a **branch**, open a **pull request**, and describe what changed plus the evidence (Section 12).
- **Standing push and merge authority (D-020):** The owner authorizes this and future agents to publish project source, tests, CI workflows, assets, and documentation to `esotericode/microjump`, push their working branches, and merge pull requests into `main` at their discretion. No separate owner confirmation is needed for these actions. Review the diff, run the relevant checks, and confirm required CI passes before merging. Report remaining milestone gates honestly; merging an increment does not make the milestone complete.
- This authority covers the GitHub workflow. The dependency, architecture, scope, and system-configuration approval requirements elsewhere in this charter still apply.
- Keep PRs focused. Avoid unrelated rewrites.

### Stop and ask the owner before

- Adding, removing, or replacing **any dependency** (Section 4.3).
- Changing any **locked decision** in Section 4 or the world conventions in Section 5.
- Changing scope, language, platforms, or the goals in Sections 1–3 and 6–11.

Everything else: decide, then record it in the decision log (Section 15) when it is significant.

---

## 3. Non-negotiable boundaries

- **Language:** All engine and gameplay runtime code is **C++23** (D-005).
- **Original implementation:** Build the renderer, scene representation, collision and character movement, camera, resource management, runtime loop, settings, and diagnostics ourselves. Do not integrate Unreal, Unity, Godot, an existing game engine, an off-the-shelf renderer, or a third-party physics/ECS framework.
- **Allowed infrastructure:** Only the dependencies approved in Section 4.3. Each must be documented with its purpose and license.
- **Necessary language exceptions:** Shaders are GLSL (D-011); assets and configuration may use data formats. These are not permission to implement engine or game logic in another language.
- **Clean-room movement work:** Super Mario 64 is a *design* reference only. Agents writing movement or camera code must **not** load decompiled SM64 source into their context. Use descriptions of behavior (design write-ups, analyses, videos, notes in this repo) instead. If decompiled code needs studying, do it as a separate research task that produces a plain-language behavior note in `docs/`, with no code excerpts. No Nintendo assets, character likenesses, animations, or copied source code.
- **Honest reporting:** Never claim a feature works without evidence. Describe compromises and known issues truthfully.

---

## 4. Locked technical decisions

Changing anything in this section requires owner approval and a new decision-log entry.

### 4.1 Platforms and graphics API (D-003)

- **Target platforms:** **Windows and Linux** (x86-64). macOS is out of scope.
- **Graphics API:** **Vulkan only.** There is no OpenGL/D3D fallback and no multi-backend abstraction.
- **Minimum:** **Vulkan 1.3** with dynamic rendering, synchronization2, and timeline semaphores. Anything newer (Vulkan 1.4 features, mesh shaders, ray tracing) is optional, detected at runtime, and never required.
- **Windows verification:** The owner tests on their own Windows PC. Agents are responsible for producing working Windows builds (Section 12.5).

### 4.2 Build system and toolchain (D-007)

- **CMake** with **CMakePresets.json**, generated with **Ninja**.
- **Compilers:** GCC and Clang on Linux; **MSVC** on Windows. Code must build **warning-free** on all of them (warnings as errors in CI).
- **Dependencies are pinned** to exact versions and fetched with **CMake FetchContent** (or vendored in `third_party/` when that is simpler). Never depend on whatever happens to be installed on the system, except the Vulkan loader/SDK and GPU drivers.
- Debug builds enable **AddressSanitizer and UndefinedBehaviorSanitizer** on Linux where practical.

### 4.3 Dependency policy (D-006)

Prefer the smallest reasonable footprint. Anything not listed here needs owner approval first.

| Library | Purpose | License | Status |
| --- | --- | --- | --- |
| **SDL3** | Window, input, gamepads, (later) audio | zlib | Allowed |
| **volk** | Vulkan function loading | MIT | Allowed |
| **Vulkan Memory Allocator (VMA)** | GPU memory allocation | MIT | Allowed |
| **Dear ImGui** | Debug overlay and settings menus **only** — not gameplay UI | MIT | Allowed |
| **doctest** | Unit/integration test framework | MIT | Allowed |
| **nlohmann/json** | Level and settings files | MIT | Allowed |
| **stb_image / stb_image_write** | Texture decoding (when needed); PNG output for frame capture | MIT / public domain | Allowed |
| **shaderc / glslc** | Build-time GLSL → SPIR-V compilation (tool, not linked into the game) | Apache-2.0 | Allowed |
| GLM or any other math library | — | — | **Denied** — we write our own math (D-006) |
| Any engine, renderer, physics, or ECS library | — | — | **Denied** |
| glTF / mesh importers (e.g. cgltf) | — | — | Not yet — ask first |

The project is **MIT-licensed** (D-016). Only accept permissively licensed dependencies (MIT, BSD, zlib, Apache-2.0, Boost, public domain) — no GPL, LGPL, or other copyleft. Distributed builds include the license notices of bundled third-party libraries.

### 4.4 Shaders (D-011)

- **GLSL**, compiled to **SPIR-V at build time** by `glslc`. Compiled shaders ship next to the executable.
- No runtime shader compilation in shipping builds. (Hot reload during development is fine if it goes through the same compiler.)

### 4.5 Architecture guardrails (D-010)

- **No ECS.** Use plain structs and arrays. A playground with a few hundred objects does not need an entity-component system, and building one is a detour.
- **Character collision:** a **kinematic capsule** using **collide-and-slide** against static triangle-mesh world geometry, with a configurable maximum walkable slope and step height. No rigid-body simulation.
- **Levels:** a **hand-written, versioned JSON level file** placing primitives generated in code (boxes, ramps, cylinders, stairs) and light fixtures. No editor and no mesh import for now.
- **Audio:** out of scope until the playground (M4) is complete (D-014).
- **Rendering:** a simple **forward renderer** first (Section 8).

---

## 5. Conventions

### 5.1 World conventions (D-008)

| Convention | Value |
| --- | --- |
| Units | **Meters**, seconds, radians (degrees only in UI and config) |
| Up axis | **+Y** |
| Handedness | **Right-handed**; forward is **−Z** in view space |
| Depth | **Reverse-Z** (clear to 0, compare GREATER), 32-bit float depth |
| Simulation | **Fixed 120 Hz** tick with render interpolation (D-009) |

SM64 ran gameplay at 30 Hz with its own units. Any movement values inspired by it must be converted to meters and our 120 Hz tick, not copied.

### 5.2 C++ code style (D-012)

- **C++23.** No C++20 modules (tooling is still uneven); use headers with `#pragma once`.
- **Namespace:** `mj` for engine code; `mj::game` for gameplay code.
- **Naming:** `PascalCase` types and enums; `snake_case` functions and variables; `m_` prefix for private members; `k_` prefix for constants.
- **Formatting:** `.clang-format` (LLVM base, 4-space indent, 100 columns) is the authority; run it before committing. `.clang-tidy` added in M0.
- **Errors:** engine code does not throw. Recoverable errors return `std::expected<T, Error>`; unrecoverable errors log and abort with a clear message. Leave compiler exception/RTTI settings at defaults so dependencies keep working.
- **Vulkan validation layers are always enabled in debug builds.** Any validation error is a bug and fails the test run.
- Text is UTF-8; file paths go through `std::filesystem`. Compiler- or OS-specific code lives only in the platform layer.

### 5.3 Repository layout

```
engine/      static library: platform, core, math, render, collision, scene, settings
game/        the playground executable; uses the engine only via its public headers
shaders/     GLSL sources
assets/      levels (JSON) and any original assets
tests/       unit tests, simulation tests, input replays, render smoke tests
tools/       helper scripts and utilities
docs/        design notes, research notes, decision details when they outgrow the log
third_party/ vendored dependencies (if any)
```

---

## 6. The first playable experience

Build a **third-person 3D movement playground** using simple, original geometric assets: a readable player placeholder, platforms, ramps, walls, steps, pillars, and a handful of light fixtures. It is a test level, not a full content-heavy game. The goal is a place that invites experimentation with speed, turning, jumps, wall jumps, landing, and camera movement.

### Required player verbs

- Walk, run, accelerate, turn, brake, and change direction with satisfying momentum.
- Jump with responsive takeoff and controllable jump height; maintain meaningful but not unlimited air steering.
- Wall jump from clearly designated surfaces with predictable angles and recovery.
- Perform at least one expressive momentum-based move (such as a long jump), once the core jump works.
- Land cleanly on platforms; handle slopes, edges, steps, and collisions without snagging or jitter.
- Reset to a nearby checkpoint after falling; restart tests without long downtime.

### What “inspired by Super Mario 64, but improved” means

Use **Super Mario 64** as a movement-design reference: directional acceleration, momentum, commitment while turning, expressive jumping, and rewarding mastery. Do **not** target identical physics values, animation timing, or a one-to-one clone. We are free to improve input response, airborne control, camera behavior, collision stability, and accessibility using contemporary techniques. (Respect the clean-room rule in Section 3.)

Candidate quality-of-life improvements include **jump buffering**, a short **coyote-time** window, optional gamepad dead-zone tuning, adjustable sensitivity, and clearer feedback for jump and wall-jump windows. Tune these with playtests; do not make movement frictionless or remove its skill ceiling.

All movement tuning values live in data or a single tuning struct, adjustable at runtime from the debug UI, so the owner can tweak feel while playing.

---

## 7. A camera that feels good

The camera is a first-class gameplay system, not an afterthought.

- Smooth third-person follow and orbit with mouse and gamepad input.
- Stable target framing while standing, moving, jumping, and falling.
- Sensible yaw/pitch limits, smoothing, configurable sensitivity, and optional inverted Y.
- Camera obstruction handling that prevents walls from hiding the player or causing harsh snaps.
- Minimal input latency; smoothing must not make the character feel disconnected.
- Camera behavior must not change with render resolution, FPS cap, or VSync.

---

## 8. Real-time lighting that earns its place

Lighting must affect the environment, support depth perception, and look good on simple geometry. A permanently baked scene with decorative light colors is not sufficient.

### First lighting target

- A **real-time directional light** (sun/key light) that casts dynamic shadows from platforms, walls, and the player.
- Several **movable point and/or spot lights** with visible, sensible attenuation and material response.
- Shadow maps with practical filtering and bias controls; avoid obvious jagged edges, acne, and detached shadows.
- Correct linear-light calculations, sRGB output, ambient fill, and tone mapping/exposure.
- Materials with readable roughness/specular response, so light position and surface properties visibly matter.
- A day/night or moving-light demonstration that proves shadows and lighting update live.

Use a **simple forward-rendering pipeline first**. Do not build deferred rendering, global illumination, ray tracing, or a complex material graph merely to appear sophisticated. Improve quality incrementally; make heavier features (e.g. shadow quality) adjustable in settings. Ambient occlusion, cascaded shadows, and more advanced effects are **stretch goals**, not prerequisites for the initial playable build.

---

## 9. The prototype playground

Create one substantial, contiguous **graybox test environment**, approximately **200–250 meters across** as an initial design target (adjust after movement playtesting). Build it from engine-generated primitive meshes placed by the JSON level file.

Include these connected testing zones:

1. **Central plaza:** an open starting area for acceleration, tight turns, braking, and camera orbiting.
2. **Sprint lane:** long, mostly flat ground with distance markers to evaluate top speed and motion feel.
3. **Platform field:** roughly **30–50 platforms** of varied heights, spacing, and widths, including forgiving routes and optional precision routes.
4. **Wall-jump section:** several paired walls, corners, and vertical routes that teach and test wall jumping.
5. **Slope and step course:** ramps, stairs, edges, narrow paths, and ledges to expose collision problems.
6. **Lighting showcase:** a shaded structure and a few colored/movable lights that create meaningful contrast with outdoor directional light.
7. **Safe reset points:** checkpoints or automatic respawns, with an easy route back to every testing zone.

These are **planning targets, not rigid asset quotas**. Make the level enjoyable to traverse. Avoid filling empty space with arbitrary objects.

---

## 10. Engine scope: build the essentials, not everything

**Required engine systems for the first prototype**

- Platform layer (SDL3): application and window lifecycle; mouse/keyboard and gamepad input.
- Main loop, monotonic timing, fixed 120 Hz simulation, render interpolation.
- Core math and transforms (our own); camera; scene objects; mesh submission and resource handles.
- Original Vulkan rendering pipeline, lighting, dynamic shadows, and simple material handling.
- Static world collision and a robust capsule character controller; basic spatial queries (raycasts, sweeps).
- Level loading from the versioned JSON format.
- Debug overlays, runtime settings, logging, frame capture, input record/replay, and tests.

**Not required initially**

A general-purpose editor, multiplayer, networking, audio, sophisticated skeletal animation, full rigid-body simulation, procedural open worlds, complex AI, cinematic tools, ray tracing, a second graphics backend, or an asset import pipeline. Add these only after a deliberate project decision.

---

## 11. Display settings and FPS overlay

The game runs at whatever frame rate it runs at. **We do not set FPS targets, and agents do not do performance testing or benchmarking** (D-004). The owner checks performance themselves. What *is* required is that the player can see the frame rate and control display settings.

### Runtime settings (required)

- **VSync:** On / Off, with the effective setting shown.
- **FPS cap:** Uncapped, plus selectable caps (e.g. 30, 60, 120, 144, 240).
- **Resolution:** Selectable resolutions; windowed, borderless, and fullscreen modes as the platform allows.
- **Shadow quality:** A simple preset (e.g. Low / Medium / High) with a sensible default.
- **Persistence:** Settings are saved to the user's config directory, survive restart, and fall back safely to defaults if invalid.

### Overlay (required)

- A toggleable on-screen readout of **FPS and frame time**, plus the current resolution and VSync/cap state.
- Debug views (separately toggleable) for **collision shapes** and the **current movement state**.

### The one correctness rule

Changing the FPS cap, VSync, or resolution **must not** change jump height, speed, collisions, or camera behavior. This is a gameplay correctness requirement, not a performance one, and it is covered by automated simulation tests (Section 12.2).

---

## 12. Verification and evidence

Agents verify their own work on the local machine before opening a PR.

### 12.1 Unit tests
Math, collision queries, level parsing, settings load/save, and other pure logic. Run via `ctest`.

### 12.2 Deterministic simulation tests
The simulation must be deterministic for a given input sequence. Tests feed recorded input (`--replay`) or scripted input and assert outcomes (positions, apex heights, landing states) within tolerances. The same input must produce the same result regardless of render rate, FPS cap, or VSync.

### 12.3 Render smoke tests and screenshots
- Launch, render a fixed number of frames, capture with `--capture-frame`, exit cleanly, with **zero Vulkan validation errors**.
- For visual changes (rendering, lighting, level layout), agents capture screenshots and **inspect them**, and attach before/after captures to the PR.

### 12.4 Playtest handoff
Movement and camera *feel* cannot be verified by code inspection. When a change needs human judgment, the agent adds a request to the PR in this format, then continues with other work rather than declaring the feel “done”:

```
PLAYTEST REQUEST
Build/branch: <branch or commit>
How to run:   <exact command>
Try:          <specific actions, e.g. "long jump from the sprint lane at full speed">
Look for:     <what to judge, e.g. "does landing feel abrupt?">
Tunables:     <parameter names and where to change them in the debug UI>
```

### 12.5 Windows builds
- A **GitHub Actions** workflow (added in M0) builds the project with MSVC on a Windows runner on every PR and every push to `main`.
- It uploads a **ready-to-run zip**: the executable, compiled shaders, assets, and required DLLs (e.g. `SDL3.dll`). No installer. The Vulkan runtime comes with the GPU driver.
- When something is ready for Windows testing, the agent confirms the Windows build passes and posts the download link in the PR.
- Optional: a local MinGW-w64 cross-compile as a fast portability check.

---

## 13. Development roadmap and milestone gates

| Milestone | Deliverable | Exit evidence |
| --- | --- | --- |
| **M0 — Foundation** | Repo layout, CMake presets, pinned dependencies, formatting/tidy config, Linux + Windows CI builds, an SDL3 window opens, Vulkan instance/device created with validation layers. | Clean builds on both OSes, the commands in Section 2 filled in, tests run in CI. |
| **M1 — First pixels** | Vulkan renderer draws a 3D scene; controllable debug camera; stable main loop; frame capture; FPS overlay. | Captured screenshots, smoke test passing with zero validation errors, resize works, Windows zip runs on the owner's PC. |
| **M2 — Movement slice** | Capsule character controller with run, jump, air steering, collision, and responsive camera; input record/replay. | Deterministic simulation tests passing; playtest request answered by the owner. |
| **M3 — Lighting slice** | Dynamic sun shadows, local lights, material response, shadow quality setting. | Moving-light demo and before/after captures. |
| **M4 — Full playground** | Traversable large graybox, platforms, wall jumps, slopes, checkpoints. | All zones playable; collision/regression checklist passes. |
| **M5 — Settings and polish** | Complete display settings, persistence, tuning, stability fixes. | Settings verified on Linux and Windows; no known critical crashes or regressions. |

Every milestone ends with a **runnable build on both platforms**, recorded observations, and a truthful statement of what does not yet work. Milestones may be split or reordered by a documented decision; they must not be silently marked complete.

---

## 14. Rules for AI programming agents

1. **Read this charter before planning work.** Keep its purpose and constraints intact.
2. **Design the smallest testable increment.** Prefer thin vertical slices over long stretches of infrastructure.
3. **Own your implementation.** Do not import an existing engine subsystem to bypass the experiment.
4. **Don't over-engineer.** No ECS, no multi-backend abstractions, no frameworks “for later.” Build what the current milestone needs.
5. **Explain significant choices.** Record architecture decisions, tradeoffs, and rejected alternatives in the decision log.
6. **Prove completion.** Build, test, run the game, inspect captures, and put exact commands and results in the PR.
7. **Protect integration quality.** Avoid unrelated rewrites; coordinate changes to public interfaces and core architecture.
8. **Treat feel as testable iteration.** Expose movement and camera tuning parameters; request playtesting rather than asserting a mechanic is “perfect.”
9. **Do not hide failures.** If something is broken or missing, document it, its suspected cause, and the next experiment.
10. **Do not rewrite the charter to declare victory.** Changes to scope, language, platforms, locked decisions, or quality goals require explicit owner approval.

### Definition of done for any feature

A feature is **Done** only when it is implemented in project-owned code, builds warning-free on Linux and Windows (CI), has appropriate automated tests or a documented manual test procedure, has been exercised in the running game, produces no Vulkan validation errors, introduces no known critical regression, and includes a short evidence note in the PR. “Compiles” alone is not Done.

---

## 15. Living project record — agents update this section

**Update policy:** Sections 1–14 are the project's guiding intent; change them only with owner approval. Agents update this section in their PRs after completing work or discovering a material issue. Never invent results.

### Current snapshot

| Field | Current value |
| --- | --- |
| Overall status | **IMPLEMENTING** |
| Current milestone | **M4 — Full playground** (in progress; JSON levels, large layout, designated wall kicks and running long jump implemented; full traversal/playtest remains). M2/M3 owner checks stay open. |
| Latest working build | 2026-10-16: momentum model (clean-hop timing, capped long-jump chains, slope speed, fixed long-jump/wall-kick arcs); `docs/movement-momentum.md`. Gamepad evidence in `docs/gamepad-support.md`; M4 moves in `docs/m4-moves.md`. |
| Open owner decisions | None. Owner input needed: M2 playtest, M3 visual review (both PR requests), Windows hardware checks for M0–M3. |
| Next useful action | Owner movement playtest of the momentum model (PLAYTEST REQUEST in `docs/movement-momentum.md`), then the remaining platform/corner traversal checklist; owner Windows/gamepad checks remain open. |

### Milestone status tracker

| ID | State | Evidence / notes |
| --- | --- | --- |
| M0 | In progress | 2026-10-15: merged PR #3; all five Linux/MSVC CI jobs passed and Windows zip inspected. Owner Windows GPU check remains open. |
| M1 | In progress | 2026-10-15: 3D scene, debug camera, GPU capture and FPS overlay implemented; local evidence/captures in `docs/m1-evidence.md`. Owner Windows run/controls acceptance remains open. |
| M2 | In progress | 2026-10-15: capsule controller, run/skid/variable jump/coyote/buffer/air steering, follow camera with obstruction, record/replay, live tuning; deterministic tests pass on three Linux configs (`docs/m2-movement.md`). Owner playtest and Windows run remain open. |
| M3 | In progress | 2026-10-15: HDR forward renderer and tone mapping, texel-snapped sun shadow map with Low/Medium/High PCF presets and bias controls, 16 point/spot lights, GGX materials, sky and day/night, lighting pavilion and moving-light demo; captures and tests in `docs/m3-lighting.md`. Owner visual review and Windows run remain open. |
| M4 | In progress | 2026-10-15: versioned JSON levels, 230 m connected ground, 40 route platforms, sprint/stair/ramp courses, designated wall kicks, running long jump and 12 checkpoints; `docs/m4-moves.md`. Scripted spawn-to-wall-finish replay and focused taller-pair fixture pass. Full traversal/owner acceptance remains. |
| M5 | Not started | — |

**Allowed states:** Not started · In progress · Blocked · Done · Reopened. State changes require a date and evidence reference (PR, commit, test log, or capture).

### Decision log (append entries)

| Date / ID | Decision | Reason and consequences |
| --- | --- | --- |
| 2026-10-15 / D-001 | Custom engine in modern C++. (Language standard updated by D-005.) | Strong LLM familiarity and mature tooling, while keeping the custom-engine experiment. |
| 2026-10-15 / D-002 | One original 3D platformer playground is the initial proving ground. | Focuses evaluation on engine quality, movement, and lighting. |
| 2026-10-15 / D-003 | Targets **Windows + Linux**; **Vulkan 1.3** only. | One modern API covers both OSes; no fallback backend; macOS out of scope. Owner tests Windows; agents produce Windows builds. |
| 2026-10-15 / D-004 | **No FPS targets, benchmarks, or agent performance testing.** Required: FPS overlay plus resolution / FPS cap / VSync settings. | Owner evaluates performance personally; agent effort goes to features and correctness. Replaces the v0.1 performance targets and benchmark protocol. |
| 2026-10-15 / D-005 | **C++23** baseline (was C++20). | GCC 15 and current MSVC support the needed features; `std::expected` defines error handling. No C++20 modules. |
| 2026-10-15 / D-006 | Dependency allowlist (Section 4.3): SDL3, volk, VMA, Dear ImGui (debug/settings only), doctest, nlohmann/json, stb, shaderc. Own math library. | Plumbing comes from small libraries; core engine systems stay ours. New dependencies need owner approval. |
| 2026-10-15 / D-007 | CMake + presets + Ninja; GCC/Clang/MSVC; pinned dependencies via FetchContent or vendoring. | Identical dependency versions on both OSes; reproducible builds. |
| 2026-10-15 / D-008 | World conventions: meters, +Y up, right-handed, reverse-Z. | Prevents inconsistent axes and units across agents. |
| 2026-10-15 / D-009 | Fixed 120 Hz simulation tick with render interpolation. | All movement tuning depends on the tick; fixing it early avoids retuning. |
| 2026-10-15 / D-010 | No ECS; kinematic capsule collide-and-slide controller; JSON levels placing code-generated primitives; forward renderer first. | Prevents over-engineering; matches the scale of one graybox playground. |
| 2026-10-15 / D-011 | Shaders in GLSL compiled to SPIR-V at build time with `glslc`. | Simplest Vulkan shader path; switching later would mean rewriting every shader. |
| 2026-10-15 / D-012 | Code style: namespace `mj`, naming per Section 5.2, clang-format, no exceptions in engine code, validation errors fail tests. | Consistent code across agents and sessions. |
| 2026-10-15 / D-013 | Process: one agent at a time (until at least M2); branch + PR per task; originally owner merges. **Push/merge authority superseded by D-020.** | Serial work and focused PRs avoid integration churn. |
| 2026-10-15 / D-014 | Audio out of scope until M4 is complete. | Scope discipline. |
| 2026-10-15 / D-015 | Clean-room rule for SM64 references (Section 3). | Reduces the risk of derived code from decompiled copyrighted source. |
| 2026-10-15 / D-016 | Project license: **MIT** (see `LICENSE`). | Simple, widely understood, compatible with every allowed dependency. Copyleft dependencies are excluded; Windows/Linux build zips include third-party license notices. |
| 2026-10-15 / D-017 | M0 selects one queue family supporting both graphics and presentation; RAII owns SDL/window and Vulkan resources. | Keeps the first device path small; separate graphics/present queue families are currently unsupported and reported clearly. No multi-backend layer. |
| 2026-10-15 / D-018 | Fetch only SDL3, volk and doctest in M0, pinned by commit and archive hash; other approved libraries wait until needed. | Minimal dependency footprint; license notices ship with runtime packages. |
| 2026-10-15 / D-019 | Linux startup tests use a Fontconfig-only LSan suppression; ASan/UBSan and other leak checks stay enabled. | System Wayland libdecor/GTK retains font caches after SDL shutdown. Evidence and scope in `docs/m0-evidence.md`; no new linked dependency. |
| 2026-10-15 / D-020 | Agents have standing authority to push project branches to `esotericode/microjump` and merge PRs into `main` at their discretion, without repeated owner confirmation. | Owner explicitly requested this on 2026-10-15 and authorized publishing/merging the M0 implementation. Replaces owner-only merging in D-013; preserves focused PRs, evidence, required checks, serial agent work, and other approval boundaries. |
| 2026-10-15 / D-021 | Proceed with M1 while owner Windows hardware acceptance for M0 remains open. | Owner explicitly requested M1 implementation, local testing, push and merge when checks pass. No Windows acceptance gate is silently waived. |
| 2026-10-15 / D-022 | M1 renders to per-frame RGBA8 sRGB/D32 targets, then blits to an sRGB FIFO swapchain; timeline completion plus per-image presentation semaphores. | Project-owned Vulkan 1.3 draw/capture path; direct memory allocation is sufficient for these few resources, so VMA is deferred. Resize waits idle and rebuilds targets. |
| 2026-10-15 / D-023 | Use approved Dear ImGui 1.91.9b for diagnostics and pinned stb for PNG output/verification; fixed 120 Hz camera translation with interpolated position and immediate mouse look. | Small M1 slice; no gameplay UI, imported renderer/math, runtime shader compilation or input replay claim. |
| 2026-10-15 / D-024 | Extend the D-019 LSan suppression to `libdecor-gtk.so` frames (the same system Wayland decoration plugin). | Native Wayland GPU tests failed intermittently (~1 in 3) on a 96-byte `wl_proxy` allocated inside the plugin's `libdecor_frame_commit`; never seen under X11/CI. Only plugin frames match; SDL, Vulkan, ImGui and engine allocations stay checked. |
| 2026-10-15 / D-025 | Proceed with M2 while owner Windows hardware acceptance for M0/M1 remains open. | Owner asked the agent to review the latest work and begin the next steps. No acceptance gate is waived; M0–M2 stay *In progress* until checked. |
| 2026-10-15 / D-026 | Collision: one-sided static triangle BVH built from the rendered triangles; capsule sweeps by conservative advancement on exact segment–triangle distance; collide-and-slide with step-up, ground snap and a max walkable slope. Steep faces are vertical walls for horizontal motion (also in the air). Steps land only on walkable faces no higher than `step_height`. | Alternatives: exact Minkowski capsule–triangle sweeps (much more code, same results), discrete overlap resolution (tunnels at speed). Advancement is short, robust and cannot tunnel. One-sided queries need closed CCW primitives, which all generated shapes are. |
| 2026-10-15 / D-027 | Record/replay stores per-tick `TickInput` (camera-relative stick, camera yaw/pitch, jump held/pressed, respawn) in versioned, run-length-encoded text with shortest round-trip floats. Press edges are latched from events until a tick. | Recording ticks, not frames, makes replay independent of render rate by construction. Tuning is not recorded (replays assume defaults). Bit-identical results are claimed only for the same build. |
| 2026-10-15 / D-028 | Renderer: per-frame uniform buffer (view-projection, sun, camera, blob shadow) plus 64-byte per-draw push constants (rigid model rows, material); back-face culling; world-space 1 m grid; a blob contact shadow under the player as a depth cue until M3 shadow maps. | Keeps push constants within the guaranteed 128 bytes and gives M3 lights a home. The blob shadow is explicitly temporary, not a substitute for the M3 lighting target. |
| 2026-10-15 / D-029 | M2 level is a compact code-defined test yard in the `Level` struct (primitives, checkpoints, kill height); JSON loading and the 200 m playground stay in M4. Gamepad input uses the approved SDL3 gamepad API. | Smallest increment that exercises every collision case M2 needs. The `Level` struct is the future JSON schema's in-memory form. The gamepad path is implemented but untested on hardware. |
| 2026-10-15 / D-030 | M2 review preserves immediate jumps and floor contact when optional grace/snap values are zero; camera obstruction takes precedence over preferred boom length. Replay limits use overflow-safe arithmetic, and empty recordings finish at tick zero. | Confirmed edge cases have regression coverage. Replay integration checks require a clean process exit as well as success text, so shutdown sanitizer failures cannot be hidden by CTest's output matching. Historical libdecor failure analysis and remaining gates: `docs/m2-review.md`. |
| 2026-10-15 / D-031 | Proceed with M3 while M2's owner playtest and M0–M2 Windows GPU checks remain open. | Owner reviewed the merged M2 work and asked to continue with the next part. No gate is waived; milestones stay *In progress* until checked. |
| 2026-10-15 / D-032 | Lighting pipeline: HDR `RGBA16F` forward pass (sky, then lit geometry) → exposure + Khronos PBR Neutral tone map → sRGB target. One D16 sun shadow map, orthographic, centered on the player and snapped to texels, with presets Low/Medium/High (1024/2048/4096², 60/80/90 m, 1/9/25 bilinear PCF taps). Constant + slope depth bias and texel-scaled normal offset are runtime-adjustable. The moon is the shadowed light at night. | Smallest pipeline meeting Section 8. Cascaded shadows and AO stay stretch goals, so distant objects are unshadowed. Tone map chosen to keep the graybox palette faithful; ACES-style curves shift hues. |
| 2026-10-15 / D-033 | Materials are per-vertex roughness/metallic with GGX/Smith/Schlick specular + Lambert diffuse and hemisphere + sky-reflection ambient. Up to 16 forward-shaded point/spot lights with windowed inverse-square falloff, placed as level `LightFixture` data (orbiting or night-only). Local lights cast no shadows. | Gives readable material response on primitives without textures or a material graph. Fixtures are level data so M4's JSON can place them. |
| 2026-10-15 / D-034 | Keep the player's contact (blob) shadow alongside sun shadows (35% default, toggle + strength in F2). | It shows the landing spot straight down when the sun's shadow falls sideways or the player is in shade. Supersedes D-028's "temporary" note; the owner's playtest decides. |
| 2026-10-15 / D-035 | Ignore relative mouse motion for 150 ms after capture engages; never capture the mouse during `--replay`. Add `--capture-tick` and switch replay capture tests to it. | Wayland pointer lock delivered a one-off camera jump that made captures irreproducible. Frame-numbered replay captures failed on slow software Vulkan once frames got heavier. |
| 2026-10-15 / D-036 | Tick captures stop at the exact simulation tick and render its current state without interpolation; light animation uses the same time. | M3 review reproduced tick 1 capturing tick 3. An integration regression now verifies exact tick-1 output and clean shutdown. Implicit fixture frame limits cannot end tick captures early. Evidence: `docs/m3-review.md`. |
| 2026-10-15 / D-037 | Proceed with M4's JSON/layout increment while the M2/M3 owner playtests and Windows GPU gates remain open. | Owner explicitly asked to check the other agent's work and continue. M3 was reviewed, corrected and merged after all five CI jobs passed. No prior gate is waived. |
| 2026-10-15 / D-038 | Activate charter-approved nlohmann/json 3.12.0, pinned by immutable commit and SHA-256, for version-1 level parsing. Validate schema/types/ranges and return errors before creating a window. | The next engine slice needs authored JSON placements. Rendering/collision still use our generated primitives. Keep the original C++ yard as a regression fixture and a matching JSON file for existing recordings; ship the parser's MIT notice. |
| 2026-10-15 / D-039 | M4's first increment is a 230 m JSON playground with 40 raised route platforms, 10 checkpoints, seven named diagnostic regions, extended sprint/stair courses and the retained lighting pavilion. | Delivers a reviewable data/loading/layout slice before adding new movement verbs. Ground bypasses and ramp returns connect the zones. Wall jumping, momentum movement and full traversal checks remain required M4 work. |
| 2026-10-15 / D-040 | Add original wall kicks on designated near-vertical triangle faces, with buffered/contact-grace input, fixed outward/upward launch, brief steering recovery and a different-facing-wall requirement before another airborne kick. Purple paired walls end at checkpoint decks. | A contact-based move reuses our capsule sweeps and avoids an invisible proximity/raycast assist. Alternating faces enables climbing; repeated boosts against one face are blocked until landing. Values remain provisional F2 tunables for owner playtest. |
| 2026-10-15 / D-041 | Add a grounded running long jump with Shift / gamepad left shoulder + jump; lower arc, bounded speed multiplier, retained landing momentum. Write replay version 2 for its modifier bit and continue reading version 1 with the original mask. | Delivers the required expressive momentum verb without a new crouch state or unbounded speed stacking. Keep the existing replay columns and exact tick sampling; old recordings remain regression fixtures. |
| 2026-10-16 / D-042 | Complete the existing SDL-mapped gamepad path with a platform-owned device lifecycle, startup/reconnect/backup selection, ordered shoulder+jump edges, focus guards, controller menu navigation, recenter/walk/overlay controls, device/button prompts and per-session stick dead zones. | Uses the already approved SDL/ImGui infrastructure and unchanged replay format. Actual SDL virtual devices exercise headless input plus the production window/gameplay/tuning UI; physical controller/transport and Windows GPU acceptance remain owner gates. Persistence/remapping/rumble stay outside this increment. |
| 2026-10-16 / D-043 | Momentum model: speed above run speed is built by long jumps (first launch unchanged, chained launches add a fixed boost) and downhill running, capped at `max_speed`; kept only by takeoffs within `hop_window` of touchdown (buffered/mashed presses keep a fraction); drained by flat/uphill friction, turning, braking and air drag. Speed no longer adds jump height above run speed. Long jumps and wall kicks use fixed arcs. Slopes use face normals. | Owner asked that speed be buildable and maintainable but not accidental, informed by games with good movement. Review showed buffered hops kept long-jump speed forever and raised jumps. Alternatives: no momentum (removes skill expression), uncapped stacking (SM64 BLJ-style runaway), removing the jump buffer (loses forgiveness). Real-input M2 replay unchanged; scripted M4 tour regenerated. Values are F2 tunables pending playtest. Rationale and references: `docs/movement-momentum.md`. |
| 2026-10-16 / D-044 | Review fixes: (1) while F2 is open only controller gameplay input is withheld (it drives the menu); the keyboard keeps moving the player unless ImGui is taking typed text, and ImGui keyboard navigation is disabled. (2) Replay format 3 adds an optional `level <name>` line; the game refuses a named recording on a different level, and older recordings still load with a note. Level names reject control characters. (3) Controller prompts name buttons by family (Xbox / PlayStation / Nintendo / generic). | Section 6 asks for tuning while playing, but the gamepad increment had frozen all input with F2 open. Old M2/M3 recordings silently replayed on the new default playground. PlayStation and Nintendo pads were shown Xbox names. Alternatives: keyboard menu navigation (it would consume Space/WASD), a level path in recordings (paths differ across machines, names do not). |
| 2026-10-16 / D-045 | Frames are paced to the display: with optional `VK_KHR_present_id` + `VK_KHR_present_wait`, each loop iteration waits until the frame before last has been shown before polling input and reading the clock (2 frames queued by default, 1 selectable in F2). Without the extensions the loop is paced by swapchain images as before. Shadow-quality changes no longer call `vkDeviceWaitIdle`: each frame slot rebinds the new map after its own previous frame, and the old map is retired by timeline value. | Windows logs (NVIDIA RTX 4070 SUPER) showed that after the shadow change's device-wide wait, or at startup, the driver returned the image just presented and frames ran in pairs: 30 Hz motion at a 60 FPS readout. Our loop paced on GPU completion and read the clock at frame start, so nothing tied frames to the display. Alternatives: detecting the paired pattern and recreating the swapchain (implemented first, then dropped at the owner's request: it treats one driver's symptom with tuned thresholds); 1 queued frame by default (missed refreshes locally at High shadows under GNOME); waiting for the previous frame's GPU work (not tied to the display). Evidence: `docs/frame-pacing.md`. |

**New entry template:** `YYYY-MM-DD / D-### — Decision — Alternatives considered — Reason — Consequences — Owner approval (if required)`.

### Progress log (append entries)

- **2026-10-15 — Charter v0.1 created.** Scope and provisional targets established. No engine implementation claimed.
- **2026-10-15 — Charter v0.2.** Locked platforms (Windows + Linux), Vulkan 1.3, C++23, dependency allowlist, world and code conventions, and agent workflow. Removed performance targets and benchmarking in favor of an FPS overlay and display settings. Added verification approach (replay tests, frame capture, playtest handoff, Windows CI builds) and clean-room rule.
- **2026-10-15 — License.** Project licensed under MIT (D-016).

- **2026-10-15 — M0 foundation started.** Added CMake/Ninja presets, SDL3 window, Vulkan 1.3 device/presentation checks, validation-aware teardown, doctest/CLI/startup tests, GCC/Clang/MSVC CI, packaging and rootless local tool setup. GCC Debug/Release and Clang Debug builds and all four local tests pass on Intel MTL; formatting and static analysis pass. Evidence: `docs/m0-evidence.md`. Windows CI and owner startup evidence remain open; no rendered scene or gameplay is claimed.

- **2026-10-15 — Charter v0.3 / agent GitHub authority.** Owner authorized publishing and merging M0, and requested that this and future agents may push and merge as they see fit. Updated Section 2 and recorded D-020; technical scope and verification requirements remain in force.

- **2026-10-15 — M1 first scene.** Added original primitive geometry, reverse-Z renderer, debug flight camera, fixed tick/interpolation, toggleable FPS overlay, GPU PNG capture and resize handling. Local GCC Debug/Release and Clang Debug suites pass; native Intel and Xvfb/software-Vulkan captures inspected. Real X11 keyboard/mouse/F1 events exercised. Evidence: `docs/m1-evidence.md`; PR records CI and runtime downloads. Dynamic shadows, character movement and playable JSON levels remain later work; Windows hardware acceptance is still open.

- **2026-10-15 — M2 movement slice.**
  - What works: engine collision world (BVH, raycast, capsule sweep, depenetration) and a collide-and-slide mover with steps, snapping and slope limits; a player with acceleration/turning/skid, variable jump, coyote time, buffering and air steering; a follow camera with obstruction handling; checkpoints and respawn; `--record`/`--replay`; gamepad input; an F2 live tuning window; and a test yard.
  - Evidence: `docs/m2-movement.md`. 10/10 CTest and 24 doctest cases (4,770 assertions) on GCC Debug, Clang Debug and GCC Release. Simulation is bit-identical at 30/60/144/240 Hz and irregular cadences. A real X11-input recording (software Vulkan, ~117 FPS) replays to the same final state natively at 60 FPS. Zero validation errors.
  - Open issues: owner playtest of feel (keyboard/mouse and gamepad); gamepad untested; Windows hardware run; wall jump/long jump and JSON playground are M4.
  - Next step: owner playtest, then M3.

- **2026-10-15 — M2 review and crash investigation.** Recovered the historical 96-byte libdecor/Wayland shutdown leak trace; D-024 suppresses that external leak rather than repairing the plugin. Fixed a separately reproduced replay-count overflow abort, empty-replay live-input fallback, disabled-grace jumping, disabled-snap floor contact, and close-wall camera clipping. Replay checks now require clean exit status. Evidence: `docs/m2-review.md`; 12/12 CTests and 26 doctest cases / 4,828 assertions pass on GCC Debug/Release and Clang Debug; formatting and clang-tidy pass. Owner feel/gamepad/Windows acceptance and the previously reported intermittent resize fixture concern remain open.

- **2026-10-15 — M3 lighting slice.**
  - What works: an HDR forward renderer with tone mapping; a texel-snapped sun shadow map (Low/Medium/High PCF, bias controls); up to 16 point/spot lights; GGX roughness/metallic materials; sky, fog and a day/night cycle where the moon takes over at night; a lighting pavilion with material samples and orbiting lights; street lamps that switch on at dusk; and F2 lighting controls.
  - Fixed along the way: a startup camera nudge from Wayland pointer lock, and frame-rate-dependent replay captures.
  - Evidence: `docs/m3-lighting.md`. 16/16 CTests on GCC Debug and Release (native Intel) and on Clang Debug (Xvfb/lavapipe). 32 doctest cases. Day/night/pavilion GPU captures pass decoded-pixel lighting checks. Zero validation errors.
  - Open issues: owner visual review and frame-rate check, Windows run, no AO or local-light shadows, limited shadow coverage, camera collapsing behind waist-high obstacles (playtest note).
  - Next step: owner review, then M4.

**New entry template:** `YYYY-MM-DD — Milestone / change — What works — Evidence (PR/commit/test/capture) — Open issues — Next step`.

- **2026-10-15 — M3 review.** Reviewed lighting/resource synchronization and inspected fresh captures. Fixed capture-tick overshoot and interpolation-dependent snapshots. All 17 native Intel CTests pass, including ASan/UBSan, synchronization validation and the new exact-tick regression; 32 doctest cases / 49,824 assertions. Details: `docs/m3-review.md`; owner visual/feel and Windows checks remain open.

- **2026-10-15 — M4 JSON/layout increment.** Runtime levels now load from validated version-1 JSON with materials, lights, named regions and checkpoints; packaged default assets resolve beside the executable. Authored a 230 m playground and preserved the original yard/recordings. All 20 CTests and 37 doctest cases / 50,645 assertions pass on GCC Debug/Release and Clang Debug; zero Vulkan validation errors, formatting and clang-tidy pass. New tests cover malformed input, UTF-8 paths, legacy movement parity, all 10 checkpoints, the extended lane and summit stairs. Native GPU views of spawn, platform field, stairs and prepared wall course were inspected. Evidence and remaining work: `docs/m4-playground.md`; PR records final checks and runtime downloads. M4 remains in progress.

- **2026-10-15 — M4 wall/long-jump increment.** Added original designated wall kicks, buffered contact windows and same-wall repeat prevention; a bounded running long jump; F2 tunables and overlay feedback; purple wall pairs with 6 m / 8 m finish decks and two new checkpoints. Replay version 2 adds the modifier while preserving version-1 reading. All 21 local CTests pass on GCC Debug/Release and Clang Debug, and 46 doctest cases / 89,739 assertions pass. A continuous 2,808-tick spawn-to-finish replay and a focused taller-pair fixture cover actual course exits; real SDL Shift+Space input records/replays identically across native and software Vulkan. Native GPU captures inspected; zero validation errors, formatting and clang-tidy pass. Evidence/playtest request: `docs/m4-moves.md`; current PR records MSVC checks and runtime downloads. M4 stays in progress pending the remaining traversal checklist and owner feel/Windows/gamepad acceptance.

- **2026-10-16 — Gamepad completion and verification.** Completed startup detection, unplug/reconnect and already-connected backup takeover (including removal between event polling and sampling); fixed short shoulder+jump taps and focus/menu input leakage. Added independent stick dead zones, actual device/jump-label diagnostics, walk, camera recenter and controller-operated tuning. All 22 local CTests pass on native GCC Debug and Xvfb/software-Vulkan GCC Release/Clang Debug; final hotplug regressions pass separately. The 52-case headless suite has 89,881 assertions, plus a rendered virtual-controller fixture verifying gameplay, menu editing and replay. Zero Vulkan validation errors; ASan/UBSan, formatting and clang-tidy pass. Evidence/playtest request: `docs/gamepad-support.md`. Physical controller feel/transport, Windows GPU acceptance and existing milestone gates remain open.

- **2026-10-16 — Momentum model.** Speed above run speed now needs intent. Long jumps and downhills build it; clean hops (within 75 ms of landing) keep it; early, mashed or late hops, friction, turning, uphill and air drag lose it. Chains are capped at 22 m/s, and momentum no longer raises jump height. Long jumps and wall kicks have fixed arcs; a tapped kick had reached less than half the height of a held one. A slope-detection bug found during the work (pad edges read as downhill) was fixed with a regression test. Real-input M2 replay unchanged; scripted M4 tour regenerated (tick 2899, checkpoint 10). New `momentum_tests.cpp`; evidence and playtest request in `docs/movement-momentum.md`. Feel is unverified until the owner playtests.

- **2026-10-16 — Review fixes (D-044).** Fixes from the post-M4 review:
  - Keyboard play continues with F2 open; checked with real X11 keys under Xvfb (player ran about 10 m and jumped with the window open).
  - Replay format 3 records the level name and rejects a mismatch before any window opens; the new `cli_rejects_replay_level_mismatch` test covers it.
  - Controller prompts use per-family button names, and level names reject control characters.
  - Windows Sticky Keys note added for the Shift long jump.

- **2026-10-16 — Frame-pacing diagnostics.** The owner reported that on Windows, changing the shadow quality leaves the game no longer smooth at 60 FPS until restart; this does not reproduce on the Linux laptop. Added:
  - per-frame CPU blocking times and GPU timestamps;
  - a pacing graph and statistics in the overlay;
  - a CSV frame log with events, including mouse deltas and capture state;
  - experiment buttons, an optional refresh-snapped frame clock, and a scripted `--pacing-test`.
  Locally, steady state is unchanged after every rebuild, with zero validation errors. The cause stays open pending Windows logs; see `docs/frame-pacing.md`.

- **2026-10-16 — Windows stutter diagnosed and fixed (D-045).** Owner logs (RTX 4070 SUPER) showed paired presentation: the driver handed back the image just presented, so frames came two at a time (jitter about 13–15 ms at a 60 FPS readout). A shadow change's `vkDeviceWaitIdle` triggered it, and so did startup; recreating the swapchain cured it.
  - Frames are now paced to the display with `VK_KHR_present_wait` where available, so frame starts and game time follow shown refreshes rather than the driver's image handling.
  - Shadow changes avoid device-wide waits (0.1 ms instead of 5–9 ms locally, validation clean).
  - Local `--pacing-test` (Intel, Debug, validation): 0.12–0.14 ms jitter in every shadow, idle-wait and swapchain phase; with pacing off after an idle wait, intervals spread from 1.8 to 47 ms.
  - Windows confirmed (owner run, RTX 4070 SUPER): phases 1–8 had 0.85–0.92 ms jitter, no long frames, and exactly 2 ticks every frame. The driver often reacquired the image just shown, without effect. Pairing did not recur in the unpaced phase this run.

### Known risks and unanswered questions

- **Character feel:** Requires iterative playtesting on keyboard/mouse and gamepad by the owner; not solvable from code inspection alone. M2 defaults are first guesses.
- **Gamepad:** SDL virtual-device and rendered gameplay/menu integration tests pass (`docs/gamepad-support.md`); physical USB/Bluetooth/controller feel remains untested on the agent machine.
- **Lighting cost:** Not measured by agents (D-004). High shadows (4096², 25 taps) and many overlapping lights in the pavilion are the heaviest cases; the owner should compare presets.
- **Replay portability:** Replays are bit-identical within one build; across compilers/OSes the last float bits may differ, so committed-replay tests use small tolerances.
- **Windows drift:** Only Linux is available locally. Keep the Windows CI build green on every PR so portability bugs surface immediately.
- **Vulkan complexity:** Vulkan is verbose; keep the renderer minimal and rely on validation layers to catch mistakes early.
- **Integration quality:** Interface churn and unreviewed merges erase productivity gains; keep tests meaningful and PRs focused.

---

## 16. Research references (inspiration, not source imports)

- Super Mario 64 decompilation project (historical context): https://github.com/n64decomp/sm64

This is reconstructed copyrighted code. **Follow the clean-room rule in Section 3:** do not load it into an agent's context while writing project code; capture any insights as plain-language behavior notes in `docs/`. Prefer secondary sources that describe movement behavior.

---

**Final test of alignment:** Whenever an agent proposes a feature or shortcut, ask: *Does this help us build and understand a better original C++ engine, or produce a more enjoyable, correct movement playground?* If not, it probably does not belong in the first prototype.
