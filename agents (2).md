# PROJECT NORTH STAR
## A custom C++ game engine, proven through a 3D platformer

**Document version:** 0.1 (initial charter)  
**Status:** Planning — implementation not yet started  
**Last updated:** 2026-10-15  
**Primary artifact:** This Markdown file is the authoritative, agent-editable project charter.

> **Our north star:** Build a high-quality, high-performance 3D game engine of our own in modern C++, and demonstrate it with a polished, expressive 3D platformer. The engine is the reason for the project; the game is how we prove it works.

## 1. Why this project exists

This is an experiment in AI-led software engineering. We want to learn how effectively LLM programming agents can **design, implement, test, profile, and improve** a real low-level system over time. Success is not merely getting a character on screen. Success means building a coherent, original, understandable engine whose performance and behavior can be demonstrated and measured.

We value careful engineering over shortcuts. A small, excellent engine and one compelling playground are better than a large, fragile game. We will deliberately invest in the engine, even when an off-the-shelf tool could produce a game faster.

### Project priorities, in order

1. **Custom engine ownership and technical learning.** Our architecture and core systems must be ours.
2. **Correctness, stability, and maintainability.** Reliable behavior and code quality come before feature count.
3. **Responsive movement and camera feel.** The prototype should be genuinely enjoyable to control.
4. **Measured runtime performance.** High performance is a design constraint and an observable result.
5. **Expressive real-time lighting.** Simple geometry should still look attractive and physically convincing enough to communicate shape and space.
6. **Scope discipline.** Finish each meaningful slice before expanding the project.

## 2. Non-negotiable boundaries

- **Language:** Write all engine and gameplay runtime code in **modern C++**, starting with **C++20**. Adopt individual C++23 features only when supported by the pinned toolchain and justified.
- **Original implementation:** Build the renderer, scene representation, collision and character movement, camera, resource management, runtime loop, settings, and diagnostics ourselves. Do not integrate Unreal, Unity, Godot, an existing game engine, an off-the-shelf renderer, or a third-party physics/ECS framework.
- **Allowed infrastructure:** Use operating-system APIs, GPU APIs, standard C++ libraries, build tools, and narrowly scoped libraries for unavoidable platform tasks (for example window creation, input, audio output, image decoding). Each dependency must be documented with its purpose and license. Prefer the smallest reasonable dependency footprint.
- **Necessary language exceptions:** GPU shaders use the shader language required by the chosen graphics API; assets and declarative configuration may use data formats. These are not permission to implement the engine or game logic in another language.
- **Graphics backend:** Choose one backend for the first prototype after a short technical evaluation. Prefer a tractable implementation over maximum API complexity. Document the decision; do not build a multi-backend abstraction prematurely.
- **AI agent mandate:** Agents may consult documentation, articles, sample code, and reference implementations to understand concepts. They must independently implement project code, honor third-party licenses, and avoid copying proprietary game assets or decompiled game code.
- **Performance cannot be faked:** Never sacrifice simulation correctness, disable required lighting features, or omit stress-test objects solely to improve published benchmarks. Describe configuration and compromises honestly.

## 3. The first playable experience

Build a **third-person 3D movement playground** using simple, original geometric assets: a readable player placeholder, platforms, ramps, walls, steps, pillars, and a handful of light fixtures. It is a test level, not a full content-heavy game. The goal is a place that invites experimentation with speed, turning, jumps, wall jumps, landing, and camera movement.

### Required player verbs

- Walk, run, accelerate, turn, brake, and change direction with satisfying momentum.
- Jump with responsive takeoff and controllable jump height; maintain meaningful but not unlimited air steering.
- Wall jump from clearly designated surfaces with predictable angles and recovery.
- Perform at least one expressive momentum-based move (such as a long jump), once the core jump works.
- Land cleanly on platforms; handle slopes, edges, steps, and collisions without snagging or jitter.
- Reset to a nearby checkpoint after falling; restart tests without long downtime.

### What “inspired by Super Mario 64, but improved” means

Use **Super Mario 64** as a movement-design reference: directional acceleration, momentum, commitment while turning, expressive jumping, and rewarding mastery. Do **not** target identical physics values, animation timing, or a one-to-one clone. We are free to improve input response, airborne control, camera behavior, collision stability, and accessibility using contemporary techniques.

Candidate quality-of-life improvements include **jump buffering**, a short **coyote-time** window, stable fixed-step physics, optional gamepad dead-zone tuning, adjustable sensitivity, and clearer feedback for jump and wall-jump windows. Tune these with playtests; do not make movement frictionless or remove its skill ceiling.

The decompilation project can be studied for historical context—especially `mario_actions_moving.c`, `mario_actions_airborne.c`, and `mario.c`—but implementation must be our own. **No Nintendo assets, character likenesses, animations, or copied source code** are part of this project.

## 4. A camera that feels good

The camera is a first-class gameplay system, not an afterthought.

- Smooth third-person follow and orbit with mouse and gamepad input.
- Stable target framing while standing, moving, jumping, and falling.
- Sensible yaw/pitch limits, smoothing, configurable sensitivity and optional inverted Y.
- Camera obstruction handling that prevents walls from hiding the player or causing harsh snaps.
- Minimal input latency; smoothing must not make the character feel disconnected.
- No physics changes when changing render resolution, frame cap, or VSync.

## 5. Real-time lighting that earns its place

Lighting must affect the environment, support depth perception, and look good on simple geometry. A permanently baked scene with decorative light colors is not sufficient.

### First lighting target

- A **real-time directional light** (sun/key light) that casts dynamic shadows from platforms, walls, and the player.
- Several **movable point and/or spot lights** with visible, sensible attenuation and material response.
- Shadow maps with practical filtering and bias controls; avoid obvious jagged edges, acne, and detached shadows.
- Correct handling of linear-light calculations, sRGB output, ambient fill, and tone mapping or exposure as appropriate to the chosen pipeline.
- Materials with readable roughness/specular response, so light position and surface properties visibly matter.
- A day/night or moving-light demonstration that proves shadows and lighting update live.

Implementation strategy is flexible: choose a **simple forward-rendering pipeline first** if it meets the scene's needs. Do not build deferred rendering, global illumination, ray tracing, or a complex material graph merely to appear sophisticated. Improve quality incrementally and profile every expensive feature. Ambient occlusion, cascaded shadows, and more advanced effects are **stretch goals**, not prerequisites for the initial playable build.

## 6. The prototype playground

Create one substantial, contiguous **graybox test environment**, approximately **200–250 meters across** as an initial design target (adjust after movement-speed playtesting). Build it primarily from engine-authored primitive meshes or original modular meshes.

Include these connected testing zones:

1. **Central plaza:** an open starting area for acceleration, tight turns, braking, and camera orbiting.
2. **Sprint lane:** long, mostly flat ground with distance markers to evaluate top speed and motion feel.
3. **Platform field:** roughly **30–50 platforms** of varied heights, spacing, and widths, including forgiving routes and optional precision routes.
4. **Wall-jump section:** several paired walls, corners, and vertical routes that teach and test wall jumping.
5. **Slope and step course:** ramps, stairs, edges, narrow paths, and ledges to expose collision problems.
6. **Lighting showcase:** a shaded structure and a few colored/movable lights that create meaningful contrast with outdoor directional light.
7. **Safe reset points:** checkpoints or automatic respawns, with an easy route back to every testing zone.

These are **planning targets, not rigid asset quotas**. Make the level enjoyable to traverse. Avoid filling empty space with arbitrary objects or adding size at the expense of navigation and performance.

## 7. Engine scope: build the essentials, not everything

**Required engine systems for the first prototype**

- Native application and window lifecycle; mouse/keyboard and gamepad input.
- Main loop, monotonic timing, fixed-step simulation, render interpolation where useful.
- Core math and transforms; camera; scene objects; mesh submission and resource handles.
- Original rendering pipeline, lighting, dynamic shadows, and simple material handling.
- Static world collision and a robust character controller; basic spatial queries.
- Level loading from a clear, versioned data representation; authored geometry placement.
- Debug overlays, runtime configuration, logging, tests, and a repeatable benchmark scene.

**Not required initially**

A general-purpose editor, multiplayer, networking, sophisticated skeletal animation, full rigid-body simulation, procedural open worlds, complex AI, cinematic tools, ray tracing, a second graphics backend, or a production asset marketplace pipeline. Add these only after a deliberate project decision.

## 8. Performance is a feature

A performant engine must be **measurable, configurable, and stable**, not merely capable of reporting a large average FPS number.

### Runtime controls (required)

- **VSync:** On / Off, with the effective setting reported.
- **Frame rate:** Uncapped rendering mode, plus selectable user caps (for example 60, 120, 144, and 240 FPS where supported).
- **Resolution:** Selectable supported display/window resolutions; windowed and fullscreen/borderless options as feasible for the chosen platform API.
- **Rendering quality:** At minimum an adjustable shadow-quality preset and a sensible default.
- **Persistence:** User settings survive restart; invalid settings fall back safely.
- **Simulation independence:** Changing refresh rate, render cap, resolution, or VSync must not change jump height, speed, collisions, or camera logic.

### Live metrics (required)

Show an optional compact overlay with **instantaneous and rolling FPS**, **average/p95/p99 frame time**, basic frame-time history, render resolution, VSync/cap state, CPU frame time, GPU time when supported, draw calls, triangle counts, active lights, and approximate process/engine memory usage. Expose a debug view for collision shapes and the current movement state. Metrics should have near-zero overhead when disabled, or their overhead should be measured.

### Initial performance targets

- **Baseline acceptance goal:** At least **60 FPS at 1920×1080**, with a **p95 frame time at or below 16.7 ms**, on a clearly recorded reference machine and the standard playground scene using the agreed default lighting preset.
- **Stretch goal:** **120 FPS** at the same scene and resolution, with **p95 ≤ 8.3 ms**, if achievable without undermining visual or code quality.
- **Behavioral goal:** Consistent physics and controls at 30, 60, 120, and uncapped rendering, and with VSync enabled or disabled.
- **Benchmark protocol:** Record CPU, GPU, RAM, operating system, driver, build mode, scene revision, quality settings, warm-up duration, test duration, and measurement tool. Save results so future agents can compare runs.

**Important:** The hardware baseline is not selected yet. Until it is, the numeric goals above are *provisional* and must not be reported as verified achievements. Prioritize p95/p99 frame time and visible hitching alongside mean FPS. Profile before optimizing.

## 9. Development roadmap and milestone gates

| Milestone | Deliverable | Exit evidence |
| --- | --- | --- |
| **M0 — Foundation** | Compiler/toolchain pinned; application opens a window; builds/tests run reproducibly. | Clean build, launch instructions, basic automated checks. |
| **M1 — First pixels** | Original C++ renderer draws a 3D scene; controllable camera; stable main loop. | Recorded demo, frame timing, tested resize behavior. |
| **M2 — Movement slice** | Character controller with run, jump, air steering, collision, and responsive camera. | Repeatable movement tests and hands-on feel review. |
| **M3 — Lighting slice** | Dynamic sun shadows, local lights, material response, basic visual quality controls. | Moving-light test and before/after captures. |
| **M4 — Full playground** | Traversable large graybox, platforms, wall jumps, slopes, checkpoints. | All zones playable; collision/regression checklist passes. |
| **M5 — Performance and polish** | Settings, metrics, benchmark automation, tuning, stability improvements. | Published hardware/config results; no critical crashes or regressions. |

Every milestone ends with a **demonstrable executable**, recorded observations, and a truthful statement of what does not yet work. Milestones may be split or reordered by a documented decision; they must not be silently marked complete.

## 10. Rules for AI programming agents

1. **Read this charter before planning work.** Keep its purpose and constraints intact.
2. **Design the smallest testable increment.** Prefer thin vertical slices over months of infrastructure.
3. **Own your implementation.** Avoid importing an existing engine subsystem to bypass the experiment.
4. **Explain significant choices.** Record architecture decisions, tradeoffs, dependencies, and rejected alternatives.
5. **Prove completion.** Build, test, inspect runtime output, profile where relevant, and attach exact commands/results or evidence references.
6. **Protect integration quality.** Avoid unrelated rewrites; coordinate changes to public interfaces and core architecture.
7. **Treat subjective feel as testable iteration.** Expose movement and camera tuning parameters; request playtesting evidence rather than asserting a mechanic is “perfect.”
8. **Do not hide failures.** If a target is missed, document the regression, suspected cause, and next experiment.
9. **Do not rewrite the north star to declare victory.** Major changes to scope, language, or quality goals require explicit project-owner approval.

### Definition of done for any feature

A feature is **Done** only when it is implemented in project-owned code, builds in the pinned environment, has appropriate tests or a documented manual test procedure, has been exercised in the running prototype, introduces no known critical regression, and includes a short evidence note. “Compiles” alone is not Done.

## 11. Living project record — agents update this section

**Update policy:** Preserve Sections 1–10 as the project's guiding intent. Agents may refine details with a dated, justified decision; they should update the following records after completing work or discovering a material issue. Never invent results.

### Current snapshot

| Field | Current value |
| --- | --- |
| Overall status | **PLANNING** |
| Current milestone | **M0 — Foundation** (not started) |
| Latest working build | None yet |
| Verified benchmark | None yet; reference hardware not selected |
| Biggest open decisions | Target OS; graphics API; baseline machine; dependency policy details |
| Next useful action | Choose initial platform/API, create repository skeleton, pin compiler and CI/build checks |

### Milestone status tracker

| ID | State | Evidence / notes |
| --- | --- | --- |
| M0 | Not started | — |
| M1 | Not started | — |
| M2 | Not started | — |
| M3 | Not started | — |
| M4 | Not started | — |
| M5 | Not started | — |

**Allowed states:** Not started · In progress · Blocked · Done · Reopened. State changes require a date and evidence reference (commit, test log, performance report, or capture).

### Decision log (append entries)

| Date / ID | Decision | Reason and consequences |
| --- | --- | --- |
| 2026-10-15 / D-001 | Modern C++ engine; initial language baseline C++20. | Prioritizes LLM familiarity and mature tooling while preserving the custom-engine experiment. |
| 2026-10-15 / D-002 | One original 3D platformer playground is the initial proving ground. | Focuses evaluation on engine quality, movement, lighting, and performance. |

**New entry template:** `YYYY-MM-DD / D-### — Decision — Alternatives — Reason — Consequences — Owner/reviewer`.

### Progress log (append entries)

- **2026-10-15 — Charter created.** Scope and provisional targets established. No engine implementation or benchmarks claimed.

**New entry template:** `YYYY-MM-DD — Milestone / change — What works — Evidence (commit/test/capture) — Performance impact — Open issues — Next step`.

### Known risks and unanswered questions

- **Graphics API/platform choice:** Pick the simplest viable first target; avoid excessive backend complexity.
- **Baseline hardware:** Required before claiming performance objectives are met.
- **Character feel:** Requires iterative testing on keyboard/mouse and gamepad; not solvable from code inspection alone.
- **Lighting cost:** Dynamic shadows can dominate GPU time; keep quality and cost visible.
- **Multi-agent coordination:** Interface churn and unreviewed merges can erase productivity gains; maintain integration tests.

## 12. Research references (inspiration, not source imports)

- *Super Mario 64* decompilation repository: https://github.com/n64decomp/sm64
- Ground movement reference: https://github.com/n64decomp/sm64/blob/master/src/game/mario_actions_moving.c
- Air movement reference: https://github.com/n64decomp/sm64/blob/master/src/game/mario_actions_airborne.c
- Mario action/state reference: https://github.com/n64decomp/sm64/blob/master/src/game/mario.c

Use these to understand historical movement ideas. Verify rights and licenses before using any external material; implement original behavior and assets in our own C++ codebase.

---

**Final test of alignment:** Whenever an agent proposes a feature or shortcut, ask: *Does this help us build and understand a better original C++ engine, produce a more enjoyable movement playground, or measurably improve quality and performance?* If not, it probably does not belong in the first prototype.
