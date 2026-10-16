#!/usr/bin/env python3
"""Generates assets/levels/lantern-isles.json, the floating-island level.

The JSON is the shipped source of truth; this script is how it was laid out. Edit here and
re-run (python3 tools/levels/lantern_isles.py) to move whole islands at once, or edit the
JSON by hand for small touches. Route spacing follows the measured jumps in docs/levels.md.
"""
import json
import math
import random
from pathlib import Path

GRASS = "#86c46c"
GRASS_DARK = "#6fae5c"
EARTH = "#8a5d3e"
ROCK = "#7a6f6c"
ROCK_DARK = "#625a5c"
CRYSTAL = "#ba76d2"  # The playground's wall-kick purple: kickable walls read the same here.
STONE = "#e6dcc6"
STONE_DARK = "#c9bca2"
WOOD = "#5a3f2e"
PLANK = "#a8784f"
TRUNK = "#6e4a33"
BLOSSOM = "#f2a7c3"
BLOSSOM_LIGHT = "#f7c4d6"
AUTUMN = "#f2bf5e"
CLOUD = "#f4efe9"
TEAL = "#7fd6c8"

shapes, checkpoints, sparks, lights, zones, portals = [], [], [], [], [], []


def r(v):
    return round(float(v), 3)


def shape(kind, center, size, color, **extra):
    item = {"kind": kind, "center": [r(c) for c in center], "size": [r(s) for s in size],
            "color": color}
    for key, value in extra.items():
        item[key] = r(value) if isinstance(value, float) else value
    shapes.append(item)


def island(x, top, z, diameter, depth=None, grass=GRASS, seed=0):
    """A grassy disc with an earth band and a rocky, pointed underside. Top surface at `top`."""
    depth = depth or diameter * 0.55
    shape("cylinder", (x, top - 0.3, z), (diameter, 0.6, diameter), grass,
          material={"roughness": 0.9, "metallic": 0})
    shape("cylinder", (x, top - 1.0, z), (diameter * 0.97, 0.8, diameter * 0.97), EARTH,
          material={"roughness": 0.95, "metallic": 0})
    shape("cone", (x, top - 1.4 - depth * 0.5, z), (diameter * 0.95, depth, diameter * 0.95), ROCK,
          tip="down", taper=0.12, yaw=seed * 0.37, material={"roughness": 0.85, "metallic": 0})
    if diameter > 6:  # A second, offset spur makes the underside less of a spinning top.
        rng = random.Random(seed + 7)
        angle = rng.uniform(0, math.tau)
        off = diameter * 0.16
        shape("cone", (x + math.cos(angle) * off, top - 1.6 - depth * 0.35, z + math.sin(angle) * off),
              (diameter * 0.5, depth * 0.7, diameter * 0.5), ROCK_DARK, tip="down", taper=0.1,
              yaw=seed * 0.91, material={"roughness": 0.85, "metallic": 0})


def stone(x, top, z, diameter, color=STONE):
    """A floating stepping stone: a short drum with a small rocky point."""
    shape("cylinder", (x, top - 0.35, z), (diameter, 0.7, diameter), color,
          material={"roughness": 0.75, "metallic": 0})
    shape("cone", (x, top - 0.7 - 0.9, z), (diameter * 0.85, 1.8, diameter * 0.85), ROCK,
          tip="down", taper=0.1, yaw=x * 0.3 + z, material={"roughness": 0.85, "metallic": 0})


def slab(x, top, z, size, yaw=0.0):
    shape("box", (x, top - 0.3, z), (size, 0.6, size), STONE_DARK, yaw=yaw,
          material={"roughness": 0.75, "metallic": 0})


def tree(x, ground, z, canopy_top, canopy=(5.0, 2.4), color=BLOSSOM, trunk=0.8):
    """A trunk with a solid canopy you can stand on. Returns the canopy's top height."""
    width, height = canopy
    center_y = canopy_top - height * 0.5
    shape("cylinder", (x, (ground + center_y) * 0.5, z), (trunk, center_y - ground, trunk), TRUNK,
          material={"roughness": 0.9, "metallic": 0})
    shape("ellipsoid", (x, center_y, z), (width, height, width), color,
          material={"roughness": 0.8, "metallic": 0})
    return canopy_top


def lantern(x, ground, z, color=(1.0, 0.58, 0.22), intensity=14.0, height=2.2):
    shape("cylinder", (x, ground + height * 0.5, z), (0.16, height, 0.16), WOOD,
          material={"roughness": 0.8, "metallic": 0})
    shape("cone", (x, ground + height + 0.33, z), (0.6, 0.22, 0.6), WOOD, taper=0.2,
          material={"roughness": 0.8, "metallic": 0})
    lights.append({"position": [r(x), r(ground + height + 0.1), r(z)], "color": list(color),
                   "intensity": intensity, "range": 9})


def checkpoint(x, ground, z, yaw=0.0):
    shape("cylinder", (x, ground + 0.03, z), (1.6, 0.06, 1.6), TEAL,
          material={"roughness": 0.6, "metallic": 0})
    checkpoints.append({"position": [r(x), r(ground + 0.07), r(z)], "yaw": r(yaw)})


def spark(name, x, y, z):
    sparks.append({"name": name, "position": [r(x), r(y), r(z)]})


def zone(name, x, z, radius):
    zones.append({"name": name, "center": [r(x), 0, r(z)], "radius": radius})


def flowers(x, top, z, radius, count, seed, colors=("#fff3a8", "#ffffff", "#f7a8c8")):
    """Drawn-only grass tufts, a few with a blossom on top, scattered on a lawn."""
    rng = random.Random(seed)
    for i in range(count):
        angle = rng.uniform(0, math.tau)
        dist = radius * math.sqrt(rng.uniform(0.05, 1))
        cx, cz = x + math.cos(angle) * dist, z + math.sin(angle) * dist
        for blade in range(3):
            a = rng.uniform(0, math.tau)
            h = rng.uniform(0.28, 0.5)
            shape("cone", (cx + math.cos(a) * 0.09, top + h * 0.5, cz + math.sin(a) * 0.09),
                  (0.13, h, 0.13), GRASS_DARK, yaw=a, solid=False,
                  material={"roughness": 0.9, "metallic": 0})
        if i % 3 == 0:
            size = rng.uniform(0.2, 0.28)
            shape("ellipsoid", (cx, top + 0.42, cz), (size, size * 0.45, size), rng.choice(colors),
                  solid=False, material={"roughness": 0.6, "metallic": 0})


# Gate Isle: spawn, the home gate and the first lantern. Top at y = 0.
island(0, 0, 0, 21, depth=11, seed=1)
checkpoint(0, 0, 3)  # 0: spawn, facing north toward the stepping stones and the grove.
shape("cylinder", (-7, 0.15, 1), (3.4, 0.3, 3.4), STONE, material={"roughness": 0.7, "metallic": 0})
portals.append({"name": "Back to the playground", "position": [-7, 1.85, 1], "radius": 1.1,
                "yaw": r(math.atan2(7, -2)), "level": "playground.json", "checkpoint": 0})
shape("cylinder", (4, 0.6, -4), (1.6, 1.2, 1.6), STONE, material={"roughness": 0.7, "metallic": 0})
spark("Welcome lantern", 4, 2.2, -4)
lantern(-3.5, 0, -6.5)
lantern(5.5, 0, 5)
flowers(0, 0, 0, 9, 22, 11)
zone("Gate Isle", 0, 0, 12)

# Stepping stones north to the Blossom Grove: 2 m gaps, rising half a metre each.
for i, (x, top, z) in enumerate([(1, 0.5, -14), (-1.5, 1.0, -19.2), (1, 1.6, -24.4), (-1, 2.2, -29.4)]):
    stone(x, top, z, 3.6)
spark("Stepping glow", 1, 2.6, -24.4)
zone("Stepping Stones", 0, -22, 9)

# Blossom Grove: pink canopies are platforms, climbing to the great tree's crown.
GX, GTOP, GZ = -2, 3.0, -45
island(GX, GTOP, GZ, 27, depth=13, seed=2)
checkpoint(0, GTOP, -35.5)
shape("ellipsoid", (-5, GTOP + 0.3, -38.5), (3.2, 2.2, 3.2), GRASS_DARK,
      material={"roughness": 0.9, "metallic": 0})  # A bush: the first step up (top 4.4).
tree(-9.5, GTOP, -41.5, GTOP + 2.8, color=BLOSSOM_LIGHT)
tree(-11, GTOP, -46.5, GTOP + 4.2)
tree(-8, GTOP, -51, GTOP + 5.6, color=BLOSSOM_LIGHT)
tree(-3, GTOP, -52.5, GTOP + 7.0, canopy=(7.0, 2.8))
spark("Blossom crown", -3, GTOP + 8.0, -52.5)
# Decorative grove trees, out of the climbing line.
tree(5, GTOP, -40, GTOP + 4.5, canopy=(4.6, 2.6), color=AUTUMN)
tree(6.5, GTOP, -49, GTOP + 6.0, canopy=(5.4, 3.0))
tree(-14, GTOP, -38.5, GTOP + 3.5, canopy=(3.6, 2.2), color=AUTUMN)
shape("ellipsoid", (3.5, GTOP + 0.7, -55.5), (4.5, 2.6, 3.2), ROCK, yaw=0.5,
      material={"roughness": 0.85, "metallic": 0})
spark("Root hollow", 2.2, GTOP + 1.0, -57.6)
lantern(-1, GTOP, -42)
lantern(9, GTOP, -45)
flowers(GX, GTOP, GZ, 12, 30, 22, colors=("#ffffff", "#f7a8c8", "#f7a8c8", "#fff3a8"))
zone("Blossom Grove", GX, GZ, 15)

# West: floating slabs to the Crystal Spire. Its kick walls copy the playground's 8 m pair
# (3.2 m apart); the last kick carries you over the west wall onto the lookout tower.
# Slabs sit a running hop apart (about 6.5 m center to center), so you can keep your speed.
for x, top, z, yaw in [(-16.5, 0.4, -2.5, 0.2), (-23, 0.7, -4.5, -0.15)]:
    slab(x, top, z, 3.4, yaw)
SX, STOP, SZ = -40, 1.0, -10
island(SX, STOP, SZ, 22, depth=13, seed=3)
checkpoint(SX + 4, STOP, SZ + 4, 0)
for x in (SX - 2, SX + 2):
    shape("box", (x, STOP + 4, SZ - 6), (0.8, 8, 12), CRYSTAL, wall_jump=True,
          material={"roughness": 0.25, "metallic": 0})
TX, TZ = SX - 4.4, SZ - 8  # The tower stands against the west wall's outer face.
shape("box", (TX, STOP + 4, TZ), (4, 8, 8), STONE, material={"roughness": 0.7, "metallic": 0})
checkpoint(TX, STOP + 8, TZ + 2.2, 0)
spark("Crystal lookout", TX, STOP + 9.0, TZ - 2)
# Crystal points crown the walls and cluster at their feet (drawn only, so kicks stay clean).
for x, y, z, w, h in [(TX - 1.4, STOP + 8, TZ - 3.2, 1.0, 2.8), (TX - 1.5, STOP + 8, TZ + 3.3, 0.8, 1.8),
                      (SX + 2, STOP + 8, SZ - 1, 0.7, 2.0), (SX + 2, STOP + 8, SZ - 11, 0.7, 1.6),
                      (SX - 3.4, STOP, SZ - 1.2, 1.4, 2.6), (SX + 3.4, STOP, SZ - 3.2, 0.9, 1.6),
                      (SX + 3.2, STOP, SZ - 0.6, 1.0, 1.9), (SX + 3.6, STOP, SZ - 9, 1.1, 2.4),
                      (TX - 2.6, STOP, TZ - 4.4, 0.9, 1.7)]:
    shape("cone", (x, y + h * 0.5, z), (w, h, w), CRYSTAL, yaw=x + z, solid=False,
          material={"roughness": 0.25, "metallic": 0})
lights.append({"position": [SX, STOP + 3, SZ - 6], "color": [0.75, 0.45, 1.0], "intensity": 18,
               "range": 10})
lantern(SX + 4.5, STOP, SZ + 2)
flowers(SX, STOP, SZ, 7.5, 14, 33)
zone("Crystal Spire", SX, SZ - 4, 13)

# East: slabs to the Old Ruins, where broken columns climb to an arch.
for x, top, z, yaw in [(16.5, 0.5, -1, -0.2), (23, 1.0, -1.5, 0.25)]:
    slab(x, top, z, 3.4, yaw)
RX, RTOP, RZ = 39, 1.5, -2
island(RX, RTOP, RZ, 20, depth=11, seed=4)
checkpoint(RX - 7, RTOP, RZ, math.pi / 2)
# About 3 m apart and 1.2 m higher each: a careful hop, not a sprint.
columns = [(RX - 4, -6.5, 1.2), (RX - 1.8, -8.6, 2.4), (RX + 1.1, -9.3, 3.6), (RX + 3.8, -8.0, 4.8)]
for x, z, height in columns:
    shape("cylinder", (x, RTOP + height * 0.5, z), (1.8, height, 1.8), STONE,
          material={"roughness": 0.7, "metallic": 0})
spark("Column climb", RX + 3.8, RTOP + 5.8, -8.0)
# The arch: two tall columns and a lintel you reach from the last broken column.
for x, z in [(RX + 5.4, -5.0), (RX + 6.6, -0.8)]:
    shape("cylinder", (x, RTOP + 3, z), (1.6, 6, 1.6), STONE, material={"roughness": 0.7, "metallic": 0})
shape("box", (RX + 6.0, RTOP + 6.25, -2.9), (2.2, 0.5, 6.0), STONE, yaw=0.278,
      material={"roughness": 0.7, "metallic": 0})
spark("Ruin crown", RX + 6.0, RTOP + 7.5, -2.9)
# Fallen stones and low walls for character.
shape("box", (RX - 4.5, RTOP + 0.4, 3.5), (3.6, 0.8, 1.2), STONE_DARK, yaw=0.4)
shape("box", (RX + 2, RTOP + 0.3, 4.5), (1.4, 0.6, 1.4), STONE_DARK, yaw=0.9)
shape("box", (RX - 8.2, RTOP + 0.6, -6), (0.8, 1.2, 3), STONE_DARK, yaw=0.2)
lantern(RX - 3, RTOP, 2)
lantern(RX + 3, RTOP, -12)
flowers(RX, RTOP, RZ, 8, 16, 44, colors=("#fff3a8", "#ffffff"))
zone("Old Ruins", RX, RZ, 13)

# South: the Long Pier, then a leap only a long jump clears down to Lantern Isle.
# The deck sits 6 cm proud of the grass: a shared plane would z-fight where they overlap.
PIER_TOP = 0.06
shape("box", (0, PIER_TOP - 0.4, 22), (4, 0.8, 26), PLANK, material={"roughness": 0.75, "metallic": 0})
for z in (12, 20, 28, 34.6):  # Pilings run from below the deck to rail height.
    for x in (-2.1, 2.1):
        shape("cylinder", (x, -0.15, z), (0.3, 2.1, 0.3), WOOD, material={"roughness": 0.8, "metallic": 0})
lantern(-2.1, 0.9, 34.6, intensity=10, height=1.2)  # Standing on the end pilings.
lantern(2.1, 0.9, 34.6, intensity=10, height=1.2)
spark("Leap of faith", 0, PIER_TOP + 1.3, 39.5)  # Mid-leap: you can grab it and still fall short.
zone("The Long Pier", 0, 22, 10)

LX, LTOP, LZ = 0, -4.0, 54.5
island(LX, LTOP, LZ, 19, depth=12, seed=5)
checkpoint(0, LTOP, 47, math.pi)
# The lantern tree: rock steps spiral up to a golden canopy.
for x, z, top in [(4.5, 51, -2.7), (5.5, 55.5, -1.4), (3.5, 59.5, -0.1), (-0.5, 61.0, 1.2)]:
    shape("cylinder", (x, (LTOP + top) * 0.5, z), (2.4, top - LTOP, 2.4), STONE_DARK,
          material={"roughness": 0.85, "metallic": 0})
tree(-1.5, LTOP, 55.5, 2.5, canopy=(7.0, 2.6), color=AUTUMN, trunk=1.2)
spark("Lantern tree", -1.5, 3.5, 55.5)
lantern(-5, LTOP, 50)
lantern(5.5, LTOP, 47.5)
lantern(-6, LTOP, 59)
flowers(LX, LTOP, LZ, 8, 18, 55, colors=("#fff3a8", "#f7a8c8", "#ffffff"))
zone("Lantern Isle", LX, LZ, 12)

# Cloud Steps: stones climbing from Lantern Isle back to the pier, with a nook off the side.
for x, top, z in [(-11, -3.2, 49), (-14, -2.4, 43.5), (-14.5, -1.6, 38), (-13, -0.8, 32.5),
                  (-11, 0, 27.3), (-6.5, 0, 23)]:
    stone(x, top, z, 3.6)
stone(-20, -2.0, 40.5, 3.2, color=STONE_DARK)
spark("Cloud nook", -20, -1.0, 40.5)
zone("Cloud Steps", -13, 36, 10)

# Far scenery: drawn-only islands drifting in the haze, and a sea of clouds below.
rng = random.Random(99)
for i in range(9):
    angle = i / 9 * math.tau + rng.uniform(-0.2, 0.2)
    dist = rng.uniform(85, 130)
    x, z = math.cos(angle) * dist, math.sin(angle) * dist
    top = rng.uniform(-8, 22)
    d = rng.uniform(10, 22)
    shape("cylinder", (x, top - 0.4, z), (d, 0.8, d), GRASS, solid=False)
    shape("cone", (x, top - 0.8 - d * 0.3, z), (d * 0.95, d * 0.6, d * 0.95), ROCK, tip="down",
          taper=0.1, yaw=angle, solid=False)
    if rng.random() < 0.6:
        shape("ellipsoid", (x + d * 0.15, top + d * 0.16, z), (d * 0.35, d * 0.3, d * 0.35),
              rng.choice([BLOSSOM, AUTUMN, GRASS_DARK]), solid=False)
for i in range(46):
    angle = rng.uniform(0, math.tau)
    dist = 20 + 150 * math.sqrt(rng.uniform(0, 1))
    w = rng.uniform(18, 42)
    shape("ellipsoid", (math.cos(angle) * dist, rng.uniform(-31, -25), math.sin(angle) * dist),
          (w, rng.uniform(5, 9), w * rng.uniform(0.6, 1.0)), CLOUD, yaw=rng.uniform(0, math.pi),
          solid=False, material={"roughness": 1, "metallic": 0})

level = {
    "version": 1,
    "name": "lantern-isles",
    "title": "The Lantern Isles",
    "kill_height": -18,
    "atmosphere": {"time_of_day": 17.0, "fog": 0.75, "grid": False, "sky_below": True},
    "shapes": shapes,
    "checkpoints": checkpoints,
    "collectibles": sparks,
    "lights": lights,
    "zones": zones,
    "portals": portals,
}
assert len(lights) <= 16, len(lights)
out = Path(__file__).resolve().parents[2] / "assets/levels/lantern-isles.json"
# One shape per line keeps diffs readable.
lines = ["{"]
for key, value in level.items():
    if isinstance(value, list):
        lines.append(f'  "{key}": [')
        lines.extend(f"    {json.dumps(item)}," for item in value)
        if value:
            lines[-1] = lines[-1].rstrip(",")
        lines.append("  ],")
    else:
        lines.append(f'  "{key}": {json.dumps(value)},')
lines[-1] = lines[-1].rstrip(",")
lines.append("}")
out.write_text("\n".join(lines) + "\n")
print(f"{out.name}: {len(shapes)} shapes, {len(checkpoints)} checkpoints, {len(sparks)} sparks, "
      f"{len(lights)} lights")
