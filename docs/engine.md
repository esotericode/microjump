# The engine

The engine is a static library (`engine/`) with public headers in
`engine/include/mj/`. The game (`game/`) uses only those headers. Libraries handle the
plumbing: SDL3 for the window and input, volk for Vulkan loading, Dear ImGui for debug
UI, nlohmann/json for levels and stb for PNG output. Everything else is ours: the math,
renderer, collision, character mover, level building and diagnostics.

**Conventions:**

- **Units:** meters, seconds and radians.
- **Axes:** +Y is up, the world is right-handed, and the view looks down −Z.
- **Depth:** reverse-Z, in a 32-bit float buffer.
- **Code:** engine code returns `std::expected` instead of throwing.

| Afternoon | Night | Pavilion at night |
| --- | --- | --- |
| ![Day](captures/m3-day.png) | ![Night](captures/m3-night.png) | ![Pavilion](captures/m3-pavilion-night-a.png) |

## Main loop and time

Gameplay runs on a **fixed 120 Hz tick** (`mj/core/fixed_step.hpp`), and rendering
interpolates between the last two ticks. A run is bit-identical for the same input,
whatever the frame rate. Frame rate, VSync and window size never change jumps, speed,
collision or the camera.

Each loop iteration:

1. Waits for the display (below).
2. Polls input.
3. Runs any due ticks.
4. Builds the frame's draw list.
5. Calls `Renderer::draw`.

## Renderer

Vulkan 1.3 with dynamic rendering, synchronization2 and timeline semaphores. It is a
forward renderer, and each frame has four passes:

1. A depth-only **sun shadow** pass.
2. An **HDR** (`RGBA16F`) scene pass: the sky first, then lit geometry.
3. **Tone mapping** into an sRGB target: exposure, then Khronos PBR Neutral.
4. The **ImGui** overlay. The result is then blitted to the swapchain, and copied to a
   readback buffer for captures.

After opaque scene draws, [dust and spark particles](particles.md) blend into HDR with
soft radial edges, ambient/emissive colour, depth testing and no depth writes. They
are sorted from far to near, so overlapping puffs blend consistently.

Per-frame data goes in a uniform buffer: camera, sun, sky, fog, the shadow matrix and
up to 16 lights. Each draw sends its transform, material flags, tint and emissive
strength as 80-byte push constants. A transform may scale each axis (the animated
character squashes spheres), so normals go through the cofactor matrix. For rigid
transforms that is the rotation itself. There are two CPU frame slots. Their completion is
tracked on one timeline semaphore, and each swapchain image has its own present
semaphore. A replaced resource, such as an old shadow map, is retired until the timeline
shows that no in-flight frame still uses it. Only resizes and swapchain rebuilds wait
for the whole device, plus `Renderer::set_meshes`, which replaces every mesh when you
travel through a gate. It waits for the frames in flight, then uploads the new vertex
buffer: a loading hitch, not a per-frame tool.

**Sun shadows.** A single orthographic map follows the player, snapped to whole texels
so that edges don't crawl. It uses PCF with hardware bilinear compares, constant and
slope bias, and a normal offset in texels. The biases are adjustable in F2. Shadows
fade out at the edge of the covered area.

| Preset | Map | Coverage | Filter |
| --- | --- | --- | --- |
| Low | 1024² | 60 m | 1 bilinear tap |
| Medium (default) | 2048² | 80 m | 3×3 taps |
| High | 4096² | 90 m | 5×5 taps |

**Local lights.** Up to 16 point or spot lights from the level, with inverse-square
falloff windowed to zero at each light's range. They can orbit, or switch on only at
night. Each light has a small emissive bulb. Local lights don't cast shadows.

**Materials.** Each vertex carries roughness and metallic values. Shading is
Cook–Torrance (GGX, Smith–Schlick, Schlick Fresnel) with Lambert diffuse. Ambient light
is a sky/ground hemisphere plus a sky reflection blended by roughness.

**Sky and time of day.** The sky, sun disk and fog come from one function, so distant
geometry fades into the horizon. Below the horizon, the moon becomes the shadow-casting
light. Exposure rises at night. Time of day advances on the fixed tick but never feeds
back into gameplay.

**Contact shadow.** A soft blob under the player marks the landing spot when the sun's
shadow falls sideways. It defaults to 35% strength and can be toggled in F2.

## Presentation and frame pacing

The swapchain uses FIFO, which is VSync on. The engine also enables
`VK_KHR_present_id` and `VK_KHR_present_wait` when the driver has them, and the
startup log says which path is in use. Every present then carries an ID. Before a frame
polls input or reads the clock, `Renderer::wait_for_display` waits until the frame
before last is actually on screen. Frames therefore start once per shown refresh, and
game time advances one refresh per frame, however the driver queues or recycles images.
Without these extensions, pacing falls back to swapchain image availability.

Why this matters: on an NVIDIA Windows driver, pacing on GPU completion alone let frames
arrive in pairs. Two frames came a few ms apart, then there was a 33 ms gap: 30 Hz motion
behind a 60 FPS counter. Display pacing fixed it (owner run: 0.85–0.92 ms jitter, no long
frames, two ticks every frame).

F2 → *Diagnostics* has the controls: pacing on/off, *Frames queued* (2 by default; 1 has
less latency but missed refreshes under GNOME at High shadows), a frame-log button, and
one-shot GPU-idle, shadow-rebuild and swapchain-rebuild experiments. `--pacing-test`
runs nine 5-second phases and prints a timing summary for each. `--frame-log` writes
one CSV row per frame. See [testing](testing.md).

## Collision and the character mover

- **Collision world** (`mj/collision/`): built from the same triangles that are
  rendered, stored in a median-split BVH. It answers raycasts, upright-capsule sweeps and
  depenetration queries.
  - Sweeps use conservative advancement on the exact segment–triangle distance, so
    nothing tunnels: a 1 km move into a 5 cm wall stops at the wall.
  - Queries are one-sided, so every primitive is closed and wound CCW.
- **Character mover** (`move_character`): kinematic collide-and-slide.
  - Walkable slopes (up to 50° by default) are followed.
  - Steeper faces act as vertical walls for horizontal motion, in the air too, so they
    can't be climbed.
  - Ledges up to 0.35 m are stepped, but only onto walkable faces no higher than the
    step height.
  - Ground snap (0.3 m) keeps the capsule on stairs and descending slopes.
  - Two faces too steep to stand on can still hold a falling capsule between them: in a
    V between slopes, or a slope and a wall. When the line where they meet is no
    steeper than a walkable slope, the crease counts as flat ground, so you can jump
    out. A steeper crease, like a corner between walls, still lets you fall.
  - Faces can carry a wall-kick designation from the level.
- **Moving bodies.** A collision world can also hold bodies: each a BVH of its own
  triangles in body space. A body's pose is a translation plus a turn about +Y, never a
  tilt, so an upright capsule stays upright in body space and every query (ray, sweep,
  depenetration) runs unchanged there. `CollisionScene` pairs the static world with
  the bodies' current and previous poses, answers queries against all of them, reports
  which body a hit belongs to, and tells a rider how a point on a body moved during
  its last step. A plain `CollisionWorld` converts to a scene with every body at rest.
  - The mover and camera take a scene. A body is treated as still during one move;
    the simulation steps the bodies first, then carries riders, then moves the player.

## Platform and input

`mj/platform/` wraps SDL3: the window, high-DPI pixel sizes, refresh rate, focus, and
relative mouse capture. Mouse motion is ignored for 150 ms after capture starts, because
Wayland's pointer lock can deliver a stray jump at that moment.

Gamepads (`gamepad.*`) are discovered at startup and whenever one connects. A backup pad
takes over when the active one disconnects. Button presses and the shoulder state at
each press are latched in order, so a quick shoulder+jump tap between polls still counts.
Focus loss clears pending presses. Button prompts follow the controller family (Xbox,
PlayStation, Nintendo, generic). Dead zones are radial and adjustable separately for the
move and look sticks.

## Levels

`mj/scene/` generates box, ramp, cylinder, ellipsoid, cone and capsule meshes. Ellipsoids
are smooth, with normals scaled by the inverse of their radii. Cones are faceted, with
twelve flat sides that read as cut rock. The game turns a JSON level into one world mesh
plus collision triangles from its solid shapes. Drawn-only shapes, like clouds and grass,
add only to the mesh. See [levels](levels.md).

A level's [moving platforms](levels.md#moving-platforms) each get their own mesh, built
in body space and drawn with the pose interpolated between the last two ticks, like the
player. Their collision is added to the world as bodies. Movers skip the graybox grid,
whose world-space lines would swim across them.

Gates swap the whole level at runtime. `main.cpp` keeps the level, its geometry and the
simulation together in one object, and replaces it between frames. A travel loads the
JSON and builds the geometry, hands the new world mesh to `Renderer::set_meshes`, snaps
the character, and sets the level's time of day.

## Expedition progress

`game/progress.*` stores authored level names, spark names and checkpoint positions.
The game reconciles those identities against current level content at startup; the
simulation receives only valid authored indices and collection flags. No player pose,
movement tuning, animation or input is serialized. The main loop snapshots after a
pickup, checkpoint change, gate swap and clean exit, outside the fixed-step callback.
Gate arrivals keep their authored checkpoint and restore only the destination's finds.
New expeditions replace the complete world between frames through the same mesh-upload
path as gates, then clear progress in every world.

`uses_saved_progress` guards profile access before even querying SDL's preference path.
Automatic and diagnostic modes keep isolated initial conditions. File writes stage
beside the destination and replace atomically; an unreadable profile blocks automatic
writes until an explicit reset. See [progress](progress.md) for the schema and limits.

## Known limits

- One shadow map with 60–90 m coverage, so distant objects are unshadowed. There is no
  ambient occlusion, and local lights don't cast shadows.
- The renderer only uses a queue family that supports both graphics and presentation.
- There are no in-game display settings yet (resolution, FPS cap, VSync toggle), and
  F2 values aren't saved.
- The Linux zip uses the host's C/C++ runtime. It isn't a portable distribution bundle.
