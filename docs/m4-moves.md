# M4: designated wall jumps and running long jumps

2026-10-15. This follows the [JSON/layout increment](m4-playground.md).
M4 stays **in progress** until the full playground traversal checklist and owner
playtest are complete. Earlier movement, lighting and Windows hardware gates
remain open.

Space / gamepad A still jumps. While airborne at a **purple wall**, release and
press jump again to kick outward and up. Alternate walls to climb. The two pairs
east of the plaza now have 6 m and 8 m finish decks with checkpoint pads. Ground
bypasses still connect the courses; R / gamepad Back returns to the active pad.

For a **long jump**, run first, hold Shift / gamepad left shoulder, and press
jump. Below the speed threshold it produces an ordinary jump. Holding jump
preserves height; releasing cuts the ascent. Long jumps travel farther with a
lower arc, keep horizontal momentum on landing, and cannot multiply speed
through repeated immediate takeoffs. Airborne modifier presses grant no boost.

## Implementation and tuning

`PlayerTuning` and F2 → **Wall / long jump** contain all new speeds and times:

| Parameter | Default | Effect |
| --- | --- | --- |
| `long_jump_min_speed` | 6 m/s | Grounded running speed required; a skid cannot launch a long jump. |
| `long_jump_speed_scale` | 1.8 | Launch multiplier, bounded by run speed × multiplier. |
| `long_jump_up_speed` | 8.5 m/s | Lower vertical launch than the running ordinary jump. |
| `wall_jump_speed` | 8.5 m/s | Outward horizontal speed, along the wall's face normal. |
| `wall_jump_up_speed` | 12 m/s | Vertical kick speed. |
| `wall_contact_grace` | 0.1 s | Window after touching a designated face; zero still permits a contact-tick press. |
| `wall_recovery_time` | 0.14 s | Brief air-steering lock to separate from the wall, then normal steering resumes. |

The controller uses the existing jump buffer. A consumed press cannot trigger a
second move just because jump remains held. Landing resets the wall-kick history;
in the air, another face must differ by at least 60 degrees from the previous kick
normal. That blocks repeated boosts against the same wall while allowing paired
walls and corners. Floor/ceiling contacts and rounded edge contacts never grant
kicks. The overlay shows the movement state and available wall-contact window.

Optional JSON `wall_jump: true` marks a primitive. Its generated collision
triangles retain that designation through BVH construction and degenerate
triangle removal; collision and rendering still use exactly the same positions.
Only near-vertical faces qualify. The authored pairs use purple albedo so their
special behavior is visible. No dependency or imported movement code was added.

New recordings write `microjump-replay 2` and add button bit 8 for the long-jump
modifier. Version 1 remains readable with its original three-bit button mask;
unknown bits are rejected for each version. Both versions retain the six-column,
120 Hz text format. Keyboard modifier state at a jump press is latched alongside
the press edge, including a complete Shift+Space tap between simulation ticks.
Old executables do not support version 2. Recordings still require matching level
and tuning, and exact float equality is a same-build guarantee.

## Verification

`tests/replays/m4-moves-tour.mjr` is a **scripted** continuous route, generated from
headless simulation inputs using default tuning. It begins at the normal spawn,
runs to the sprint lane, long jumps, travels to the first wall pair, alternates
three wall kicks, lands on the finish deck and activates checkpoint 10:

```text
tick 2808, feet (64.4703598, 6.06500101, -57.9915848), state Run, grounded, checkpoint 10
```

The integration test runs that recording through the full rendered game, captures
tick 2808 and requires the expected checkpoint, a clean exit and zero Vulkan
validation errors. A focused simulation fixture also climbs the taller pair and
lands on its 8 m exit. Every checkpoint is checked for safe settling/activation.

Unit tests cover marked/unmarked faces, degenerate filtering, zero grace/buffer,
contact expiry, buffered presses, same-wall repeat prevention, held input,
respawn/landing reset, long-jump launch requirements, bounded chaining, and range
versus height. A stream containing both new moves is serialized/reloaded and
compared bit for bit on **every tick**, including wall timers and camera state,
at simulated 30/60/144/240 Hz and irregular render cadences. This verifies gameplay
correctness across frame schedules; no performance measurements are made.

Run the route with:

```sh
tools/local-env.sh ./build/linux-release/game/microjump \
  --replay tests/replays/m4-moves-tour.mjr
```

## Local evidence

All **21 CTests** passed on GCC Debug/Release with native Intel Vulkan and on
Clang Debug with Xvfb/lavapipe. The final unit suite has **46 cases / 89,739
assertions**, including the taller-pair fixture (rerun separately on GCC Debug
after the integration suite). ASan/UBSan, Vulkan synchronization validation,
clang-format and clang-tidy pass; GPU runs report zero validation errors.

The updated `tools/input-smoke.sh` exercised real X11 Shift+Space events. Its
1,925-tick recording contains a long-jump state; replaying through native Intel
Vulkan produced the identical full-precision final state:

```text
tick 1925, feet (11.4334164, 0.00683406461, -17.9495888), state Idle, grounded, checkpoint 0
```

Inspected native GPU captures (no substituted desktop screenshots):

| Long jump, tick 860 | Wall kick, tick 2648 |
| --- | --- |
| ![Long jump](captures/m4-long-jump.png) | ![Wall kick](captures/m4-wall-kick.png) |

[Finish checkpoint, tick 2808](captures/m4-wall-finish.png) ·
[Before: prepared blue wall course](captures/m4-walls.png).
The new captures use the committed continuous route with the actual default
playground, rather than changing spawn for a view fixture.

## Remaining acceptance

These are initial movement values. The scripted route and local input exercise
establish specific behavior, not approval of feel. Physical gamepad testing,
owner Windows GPU testing, a complete traversal of all platform routes, and the
known close-obstacle camera framing review remain open. No new crash is claimed
or observed; the historical system libdecor shutdown leak is documented in the
[M2 review](m2-review.md).

```text
PLAYTEST REQUEST
Build/branch: codex/m4-wall-long-jumps (runtime downloads in the PR)
How to run:   microjump.exe
              Linux: tools/local-env.sh ./build/linux-release/game/microjump
Try:          Run north on the sprint lane and compare Space with Shift+Space.
              Head east to the purple paired walls; jump into a wall, release
              and press Space again, then alternate kicks to the finish deck.
              Try the taller pair, land on its pad, then press R / gamepad Back.
Look for:     Predictable kick direction, enough time to press, recovery/steering
              comfort, long-jump landing momentum, readable purple surfaces,
              and camera framing near walls and the deck edges.
Tunables:     F2 > Wall / long jump; normal air steering under Jump and air.
```
