# Level format, version 1

The game loads `assets/levels/playground.json` beside the executable by default.
Builds copy the level files into the game directory; runtime zips include them.
An explicit `--level PATH` is relative to the process working directory, or can
be absolute. The format places engine-generated primitives, not imported meshes.

```json
{
  "version": 1,
  "name": "Small yard",
  "kill_height": -25,
  "shapes": [
    {"kind":"box", "center":[0,-0.5,0], "size":[30,1,30], "color":"#c2cac6"},
    {"kind":"ramp", "center":[5,1,-5], "size":[4,2,6], "color":"#42a5a0",
     "yaw":0, "material":{"roughness":0.45,"metallic":0}}
  ],
  "checkpoints": [{"position":[0,0.01,4], "yaw":0}],
  "lights": [{"position":[3,4,1], "color":[1,0.7,0.4], "intensity":30, "range":12,
              "night_only":true}],
  "zones": [{"name":"Plaza", "center":[0,0,0], "radius":12}]
}
```

All coordinates and dimensions are **meters**; +Y is up. Angles are **radians**.
Yaw 0 faces −Z for checkpoint/camera orientation. Primitive yaw rotates about +Y
using the mesh convention; a ramp's high edge is its local −Z edge. Color strings
are sRGB `#RRGGBB` and are converted to linear albedo once. Light colors are linear
RGB vectors; intensity is separate.

| Field | Meaning / defaults |
| --- | --- |
| `version`, `name` | Required. Version must be integer 1; names contain 1–80 UTF-8 bytes and no control characters. Recordings store the level `name` and refuse to replay on a differently named level. |
| `kill_height` | Falling below this Y respawns at the active checkpoint; default −25. |
| `shapes` | Required, 1–2048 placements. Each requires `kind`, `center`, `size`, `color`. |
| `kind` | `box`, `ramp`, or `cylinder`. A cylinder uses X/Z diameter and Y height; X and Z must match. |
| `center`, `size` | Three-number vectors. Coordinates are bounded to ±4096 m; dimensions are positive, 0.01–512 m. |
| `yaw` | Optional, default 0. Cylinders are rotationally symmetric. |
| `wall_jump` | Optional boolean, default false. Enables kicks on near-vertical faces of this shape. Use the playground's purple palette for clear designation; floors, ceilings and rounded edge contacts do not grant kicks. |
| `material` | Optional object: `roughness` defaults to 0.7, `metallic` to 0; both range 0–1. |
| `checkpoints` | Required, 1–64 objects with `position` (capsule feet) and optional `yaw`. The first is spawn. All must be above the kill plane. |
| `lights` | Optional, at most 16 fixtures. Requires `position`, `color`; the remaining fields below are optional. |
| `zones` | Optional, at most 32 named diagnostic regions. Each has `name`, `center`, and optional `radius` (default 20 m). The nearest containing horizontal region appears in the overlay. |

Each light also supports:

| Field | Meaning / default |
| --- | --- |
| `intensity`, `range` | Linear intensity at 1 m (10) and radius where falloff reaches zero (10 m). |
| `direction` | Nonzero vector, normalized on load; default `[0,-1,0]`. Components range −1 to 1. |
| `inner_cone`, `outer_cone` | Spot half-angles, 0 ≤ inner ≤ outer ≤ π, outer > 0. Both default π for a point light. |
| `orbit_radius`, `orbit_speed` | Horizontal orbit around `position`, in meters and rad/s; default 0. |
| `night_only` | Boolean, default false. Fades in after sunset using the day/night system. |

Rendering and collision are built from the same placements. Place each checkpoint
on a safe, walkable surface, just above it; there is no automatic repair of level
geometry. The committed playground has simulation tests for every checkpoint.

Malformed JSON, unsupported versions, duplicate/unknown fields, wrong types,
nonfinite/out-of-range values, invalid cones and element-count violations return
a diagnostic before a game window opens. Files are limited to 2 MiB and 32 levels
of JSON nesting. The pinned, charter-approved [nlohmann/json parser](https://json.nlohmann.me/api/basic_json/parse/)
is called with parse exceptions disabled; gameplay data is validated before use.

Recordings contain tick inputs, not the level or tuning. Replay with the same
level and default tuning. The M2/M3 regression replays use `movement-yard.json`:

```sh
./build/linux-release/game/microjump --level assets/levels/movement-yard.json \
  --replay tests/replays/m2-input-smoke.mjr
```
