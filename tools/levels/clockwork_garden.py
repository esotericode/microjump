#!/usr/bin/env python3
"""Generates assets/levels/clockwork-garden.json, the walled garden of moving machines.

The JSON is the shipped source of truth; this script is how it was laid out. Edit here and
re-run (python3 tools/levels/clockwork_garden.py) to move whole machines at once, or edit the
JSON by hand for small touches. Route spacing follows the measured jumps in docs/levels.md,
and tests/clockwork_garden_tests.cpp rides every route.

North is -Z. Spawn is on the south lawn facing the clock tower. The garden's floor catches
every fall, so the challenge is in getting up, never in surviving.
"""
import json
import math
from pathlib import Path

LAWN = "#5d8a4c"
LAWN_DARK = "#4f7a40"
GRAVEL = "#cbb995"
HEDGE = "#3b6536"
HEDGE_DARK = "#31582e"
BRASS = "#c9a24a"
BRASS_DARK = "#a7843a"
COPPER = "#b8734a"
IRON = "#4f555e"
STONE = "#d9cfbc"
STONE_DARK = "#bfb39c"
BRICK = "#a2614a"
CREAM = "#efe6cf"
INK = "#26262e"
TEAL = "#7fd6c8"
SPRING = "#d9534f"
WATER = "#5f9fbf"
HORSE = "#f1ead8"
CANOPY_RED = "#b84a4a"
CANOPY_CREAM = "#f3e7c9"

BRASS_MATERIAL = {"roughness": 0.35, "metallic": 0.9}
IRON_MATERIAL = {"roughness": 0.5, "metallic": 0.7}

shapes, checkpoints, sparks, lights, zones, portals, movers = [], [], [], [], [], [], []


def r(v):
    return round(float(v), 3)


def make(kind, center, size, color, **extra):
    item = {"kind": kind, "center": [r(c) for c in center], "size": [r(s) for s in size],
            "color": color}
    for key, value in extra.items():
        item[key] = r(value) if isinstance(value, float) else value
    return item


def shape(kind, center, size, color, **extra):
    shapes.append(make(kind, center, size, color, **extra))


def mover(name, pivot, parts, **motion):
    item = {"name": name, "pivot": [r(c) for c in pivot], "shapes": parts}
    for key, value in motion.items():
        item[key] = r(value) if isinstance(value, float) else value
    movers.append(item)


def light(position, color, intensity, reach, **extra):
    item = {"position": [r(c) for c in position], "color": list(color), "intensity": intensity,
            "range": reach}
    item.update(extra)
    lights.append(item)


def checkpoint(x, ground, z, yaw=0.0):
    shape("cylinder", (x, ground + 0.03, z), (1.6, 0.06, 1.6), TEAL,
          material={"roughness": 0.6, "metallic": 0})
    checkpoints.append({"position": [r(x), r(ground + 0.07), r(z)], "yaw": r(yaw)})


def spark(name, x, y, z, radius=None):
    item = {"name": name, "position": [r(x), r(y), r(z)]}
    if radius:
        item["radius"] = radius
    sparks.append(item)


def lamp_post(x, z, ground=0.0, height=3.2):
    """An iron lamp post with a warm lamp that lights at dusk."""
    shape("cylinder", (x, ground + height * 0.5, z), (0.18, height, 0.18), IRON,
          material=IRON_MATERIAL)
    shape("ellipsoid", (x, ground + height + 0.15, z), (0.42, 0.5, 0.42), "#ffe2a8", solid=False,
          material={"roughness": 0.3, "metallic": 0})
    light((x, ground + height + 0.15, z), (1.0, 0.7, 0.38), 16, 11, night_only=True)


def topiary(x, z, height=2.6, ball=1.8):
    shape("cylinder", (x, height * 0.3, z), (0.3, height * 0.6, 0.3), "#6b4a32",
          material={"roughness": 0.9, "metallic": 0})
    shape("ellipsoid", (x, height - ball * 0.5, z), (ball, ball, ball), HEDGE,
          material={"roughness": 0.9, "metallic": 0})


def gear_parts(radius, color=BRASS, boss=0.0, teeth_color=BRASS_DARK):
    """A flat gear whose top is at the body origin: a disc, a ring of teeth just below the
    top, drawn spokes and an optional raised hub boss."""
    parts = [make("cylinder", (0, -0.3, 0), (radius * 2, 0.6, radius * 2), color,
                  material=BRASS_MATERIAL)]
    count = max(8, round(math.tau * radius / 1.1))
    for i in range(count):
        a = i * math.tau / count
        d = radius + 0.2
        parts.append(make("box", (math.cos(a) * d, -0.325, math.sin(a) * d), (0.75, 0.45, 0.55),
                          teeth_color, yaw=a, material=BRASS_MATERIAL))
    for i in range(3):  # Spokes, drawn just proud of the face.
        a = i * math.pi / 3
        parts.append(make("box", (0, 0.015, 0), (radius * 1.7, 0.03, 0.22), teeth_color, yaw=a,
                          solid=False, material=BRASS_MATERIAL))
    if boss > 0:
        parts.append(make("cylinder", (0, boss * 0.5, 0), (1.4, boss, 1.4), COPPER,
                          material=BRASS_MATERIAL))
    return parts


def gear(name, x, top, z, radius, spin, boss=0.0, phase_yaw=0.0):
    """A turning gear on a fixed iron axle. Returns where its top is."""
    if top > 0.7:  # A low gear sits on the lawn.
        shape("cylinder", (x, (top - 0.6) * 0.5, z), (0.9, top - 0.6, 0.9), IRON,
              material=IRON_MATERIAL)
    mover(name, (x, top, z), gear_parts(radius, boss=boss), spin=spin, yaw=phase_yaw)


def slab_parts(width, depth, color=BRASS, thickness=0.5):
    return [make("box", (0, -thickness * 0.5, 0), (width, thickness, depth), color,
                 material=BRASS_MATERIAL),
            make("box", (0, -thickness - 0.08, 0), (width * 0.8, 0.16, depth * 0.8), IRON,
                 material=IRON_MATERIAL)]


def deck(x0, x1, z0, z1, top, color=STONE, thickness=0.6):
    shape("box", ((x0 + x1) * 0.5, top - thickness * 0.5, (z0 + z1) * 0.5),
          (x1 - x0, thickness, z1 - z0), color, material={"roughness": 0.75, "metallic": 0})


# --- Ground, walls and paths -------------------------------------------------------------

shape("box", (0, -0.5, 0), (128, 1, 128), LAWN, material={"roughness": 0.95, "metallic": 0})
# Gravel paths, a hair above the lawn so they never share its plane.
shape("box", (0, 0.01, 7), (5, 0.02, 64), GRAVEL, material={"roughness": 0.9, "metallic": 0})
shape("box", (0, 0.01, 18), (70, 0.02, 4), GRAVEL, material={"roughness": 0.9, "metallic": 0})
for side in (-1, 1):  # The garden wall: tall hedges all round.
    shape("box", (side * 61, 2.5, 0), (2, 5, 124), HEDGE_DARK,
          material={"roughness": 0.95, "metallic": 0})
    shape("box", (0, 2.5, side * 61), (120, 5, 2), HEDGE_DARK,
          material={"roughness": 0.95, "metallic": 0})
for z in (30, -6):  # Low hedges lining the main walk.
    for side in (-1, 1):
        shape("box", (side * 4.5, 0.5, z + 6), (1.2, 1.0, 9.0), HEDGE,
              material={"roughness": 0.95, "metallic": 0})
for x, z in ((-8, 34), (8, 34), (-12, 2), (12, 2), (-50, 40), (50, 40), (-52, -52),
             (52, -50), (24, -50), (-24, 50)):
    topiary(x, z)

# --- Gate lawn: spawn, the way home and the first tick -----------------------------------

checkpoint(0, 0.0, 40)
zones.append({"name": "Gate lawn", "center": [0, 0, 38], "radius": 12})
shape("cylinder", (-7, 0.15, 44), (3.0, 0.3, 3.0), STONE, material={"roughness": 0.7, "metallic": 0})
portals.append({"name": "Back to the playground", "position": [-7, 1.85, 44], "radius": 1.1,
                "yaw": 1.2, "level": "playground.json", "checkpoint": 0})
light((-7, 3.6, 44), (0.55, 0.75, 1.0), 10, 8)
# A small, slow gear right ahead: hop on and it turns you round.
gear("First gear", 0, 0.6, 28, 3.0, 0.4, boss=0.3)
spark("First tick", 0, 1.9, 28)

# --- Fountain plaza ----------------------------------------------------------------------

zones.append({"name": "Fountain plaza", "center": [0, 0, 10], "radius": 10})
shape("cylinder", (0, 0.3, 10), (7.0, 0.6, 7.0), STONE_DARK, material={"roughness": 0.7, "metallic": 0})
shape("cylinder", (0, 0.35, 10), (6.2, 0.6, 6.2), WATER, solid=False,
      material={"roughness": 0.08, "metallic": 0})
shape("cylinder", (0, 0.7, 10), (1.2, 0.8, 1.2), STONE, material={"roughness": 0.7, "metallic": 0})
# A brass spring in the fountain's spout throws you higher than any jump from it reaches.
shape("cylinder", (0, 1.2, 10), (1.6, 0.2, 1.6), SPRING, bounce=20.0,
      material={"roughness": 0.4, "metallic": 0.3})
spark("Fountain", 0, 8.8, 10, radius=1.0)  # The spray is wide.
light((0, 2.0, 10), (0.5, 0.8, 1.0), 12, 9)

# --- West: the gear stair ----------------------------------------------------------------

zones.append({"name": "Gear stair", "center": [-32, 0, 8], "radius": 20})
# Each gear turns the other way from the last, with the same rim speed.
GEARS = [  # x, top, z, radius
    (-20.0, 1.0, 22.0, 3.5),
    (-27.5, 2.2, 15.5, 3.0),
    (-34.0, 3.4, 8.5, 3.0),
    (-40.0, 4.6, 1.5, 2.5),
    (-45.0, 5.8, -5.5, 2.5),
]
for i, (x, top, z, radius) in enumerate(GEARS):
    spin = (1.5 / radius) * (1 if i % 2 == 0 else -1)
    boss = 0.3 if i in (2, 4) else 0.0
    gear(f"Stair gear {i + 1}", x, top, z, radius, spin, boss=boss, phase_yaw=i * 0.4)
spark("Gear stair", -34.0, 3.4 + 0.3 + 1.0, 8.5)
spark("Mainspring", -45.0, 5.8 + 0.3 + 1.0, -5.5)
light((-45.0, 8.5, -5.5), (1.0, 0.75, 0.4), 18, 10)

# A landing beside the top gear, and a ferry across to the tower.
deck(-47.5, -42.5, -15.5, -10.5, 6.4)
shape("cylinder", (-45, 3.05, -13), (1.0, 6.1, 1.0), IRON, material=IRON_MATERIAL)
checkpoint(-45, 6.4, -13, math.pi * 0.75)
mover("Tower ferry", (-38.5, 6.4, -13), slab_parts(4, 4),
      path=[[0, 0, 0], [22.5, 0, -17]], period=20.0, pause=2.0)

# --- The clock tower ----------------------------------------------------------------------

TOWER = (0.0, -35.0)
TX, TZ = TOWER
zones.append({"name": "Clock tower", "center": [TX, 0, TZ], "radius": 14})
shape("box", (TX, 12.7, TZ), (12, 25.4, 12), BRICK, material={"roughness": 0.85, "metallic": 0})
for y in (6.0, 13.0):  # Stone bands.
    shape("box", (TX, y, TZ), (12.3, 0.4, 12.3), STONE, material={"roughness": 0.7, "metallic": 0})
# Four clock faces, frozen at ten past ten (the tower's working clock is on its roof).
for face_yaw in (0.0, math.pi / 2, math.pi, -math.pi / 2):
    out = (math.sin(face_yaw), -math.cos(face_yaw))  # yaw_forward
    cx, cz = TX + out[0] * 6.1, TZ + out[1] * 6.1
    shape("box", (cx, 16.8, cz), (6.0, 6.0, 0.2), CREAM, yaw=face_yaw, solid=False,
          material={"roughness": 0.4, "metallic": 0})
    for hour in range(12):
        a = hour * math.tau / 12
        right = (math.cos(face_yaw), math.sin(face_yaw))
        hx = cx + out[0] * 0.12 + right[0] * math.sin(a) * 2.4
        hz = cz + out[1] * 0.12 + right[1] * math.sin(a) * 2.4
        shape("box", (hx, 16.8 + math.cos(a) * 2.4, hz), (0.25, 0.5, 0.06), INK, yaw=face_yaw,
              solid=False, material={"roughness": 0.5, "metallic": 0})
    for length, angle in ((1.6, -math.pi / 3), (2.2, math.pi / 3)):
        right = (math.cos(face_yaw), math.sin(face_yaw))
        # Boxes cannot tilt, so the hands are drawn as short stacked bars.
        for k in range(6):
            t = (k + 0.5) / 6
            shape("box", (cx + out[0] * 0.16 + right[0] * math.sin(angle) * length * t,
                          16.8 + math.cos(angle) * length * t,
                          cz + out[1] * 0.16 + right[1] * math.sin(angle) * length * t),
                  (0.3, 0.3, 0.06), INK, yaw=face_yaw, solid=False,
                  material={"roughness": 0.5, "metallic": 0})

# The west ledge, where the ferry docks, and the long lift up to the gallery.
deck(-12.0, -6.0, -38.0, -32.0, 6.4, STONE_DARK, thickness=0.5)  # Clear of the stone band.
checkpoint(-9.5, 6.4, -35, -math.pi / 2)
spark("West landing", -8.0, 7.5, -37.0)
mover("Gallery lift", (-14.0, 6.4, TZ), slab_parts(2.4, 2.4, COPPER),
      path=[[0, 0, 0], [0, 14.6, 0]], period=16.0, pause=2.0)

# The gallery runs all round the tower, under the overhanging dial.
deck(TX - 9, TX + 9, TZ - 9, TZ + 9, 21.0, STONE)
checkpoint(TX - 7.5, 21.0, TZ - 7.5, math.pi)
spark("Gallery", TX + 7.5, 22.2, TZ - 7.5)
for x, z in ((TX - 7.5, TZ + 7.5), (TX + 7.5, TZ - 7.5)):
    light((x, 23.5, z), (1.0, 0.72, 0.4), 14, 10, night_only=True)
# Two ways up to the dial: a short lift on the south side, and a spring in the SE corner.
mover("Dial lift", (TX, 21.0, TZ + 10.4), slab_parts(2.4, 2.4, COPPER),
      path=[[0, 0, 0], [0, 5.4, 0]], period=9.0, pause=1.5)
shape("cylinder", (TX + 7.6, 21.11, TZ + 7.6), (2.0, 0.22, 2.0), SPRING, bounce=24.0,
      material={"roughness": 0.4, "metallic": 0.3})

# The dial: the tower's roof is a great clock face with a sweeping minute hand.
DIAL = 26.0
shape("cylinder", (TX, DIAL - 0.3, TZ), (17, 0.6, 17), CREAM, material={"roughness": 0.45, "metallic": 0})
for hour in range(12):  # Brass hour marks around the rim, drawn just proud of the face.
    a = hour * math.tau / 12
    shape("box", (TX + math.sin(a) * 7.6, DIAL + 0.02, TZ - math.cos(a) * 7.6), (0.35, 0.04, 1.0),
          BRASS_DARK, yaw=a, solid=False, material=BRASS_MATERIAL)
shape("cylinder", (TX, DIAL + 0.5, TZ), (1.6, 1.0, 1.6), BRASS_DARK, material=BRASS_MATERIAL)
mover("Minute hand", (TX, DIAL, TZ),
      [make("box", (3.9, 0.37, 0), (5.8, 0.7, 0.45), INK, material=IRON_MATERIAL),
       make("box", (-1.5, 0.37, 0), (1.0, 0.7, 0.7), INK, material=IRON_MATERIAL)],
      spin=0.45)
checkpoint(TX - 5.45, DIAL, TZ + 5.45, math.pi * 0.75)  # Beyond the hand's reach.
spark("The Hour", TX, DIAL + 2.0, TZ)
# The weathervane stands on the gallery just past the dial's north rim, a triple jump above
# the dial: run the length of the dial, past the hub, and leap at the rim.
VANE = (TX + 2.0, TZ - 8.6)
shape("cylinder", (VANE[0], (21.0 + DIAL + 4.1) * 0.5, VANE[1]), (0.3, DIAL + 4.1 - 21.0, 0.3), IRON,
      material=IRON_MATERIAL)
shape("cylinder", (VANE[0], DIAL + 4.25, VANE[1]), (2.0, 0.3, 2.0), BRASS, material=BRASS_MATERIAL)
shape("box", (VANE[0], DIAL + 5.0, VANE[1]), (0.1, 0.25, 1.6), COPPER, solid=False,
      material=BRASS_MATERIAL)
spark("Weathervane", VANE[0], DIAL + 5.4, VANE[1])
light((TX, DIAL + 3.0, TZ), (1.0, 0.85, 0.6), 22, 12)

# Behind the clock: a ledge on the north face, under the gallery. Its edge peeks out past the
# gallery's, so looking over hints at it: drop off the north edge and steer back in.
deck(TX - 3.2, TX + 3.2, TZ - 10.0, TZ - 6.0, 14.0, STONE_DARK)
spark("Behind the clock", TX, 15.2, TZ - 7.4)
light((TX + 2.2, 15.0, TZ - 7.6), (1.0, 0.6, 0.3), 9, 6)

# --- East: the piston steps and the brass terrace ----------------------------------------

zones.append({"name": "Piston steps", "center": [33, 0, 24], "radius": 16})
PISTONS = [(22.0, 0.9, 0.0), (27.0, 2.4, 0.5), (32.0, 3.9, 0.0), (37.0, 5.4, 0.5)]
for i, (x, low, phase) in enumerate(PISTONS):
    shape("cylinder", (x, 0.15, 24), (2.2, 0.3, 2.2), IRON, material=IRON_MATERIAL)  # Housing.
    # The rod reaches into its housing even at the top of the stroke.
    rod = make("cylinder", (0, -(low + 3.5) * 0.5 - 0.1, 0), (0.7, low + 3.5, 0.7), IRON, solid=False,
               material=IRON_MATERIAL)
    mover(f"Piston {i + 1}", (x, low, 24), slab_parts(3, 3) + [rod],
          path=[[0, 0, 0], [0, 3, 0]], period=6.0, pause=1.0, phase=phase)
spark("Piston top", 27.0, 2.4 + 3 + 1.4, 24)
deck(41, 49, 18, 30, 9.0, STONE)
for x, z in ((42, 19), (48, 19), (42, 29), (48, 29)):
    shape("cylinder", (x, 4.2, z), (0.9, 8.4, 0.9), STONE_DARK, material={"roughness": 0.75, "metallic": 0})
checkpoint(45, 9.0, 24, math.pi)
spark("Brass terrace", 47.0, 10.2, 20.0)
light((45, 11.0, 26), (1.0, 0.75, 0.45), 14, 10, night_only=True)

# --- Northeast: the ferris wheel, its deck and the sky walk to the tower -----------------

HUB = (40.0, 12.0, -28.0)
zones.append({"name": "Ferris wheel", "center": [HUB[0], 0, HUB[2]], "radius": 14})
shape("box", (HUB[0], (HUB[1] - 0.4) * 0.5, HUB[2] + 4.0), (1.0, HUB[1] - 0.4, 1.0), IRON,
      material=IRON_MATERIAL)
shape("box", (HUB[0], HUB[1], HUB[2] + 2.1), (0.8, 0.8, 3.2), IRON, material=IRON_MATERIAL)
shape("ellipsoid", HUB, (1.8, 1.8, 1.0), BRASS, material=BRASS_MATERIAL)
# The wheel itself stands still: gondolas run round its rim like a paternoster. Its rim and
# spokes are beads behind the gondolas (shapes cannot tilt), with a lamp every few beads.
RIM_Z = HUB[2] + 1.5
for i in range(72):
    a = i * math.tau / 72
    lamp = i % 6 == 0
    shape("ellipsoid", (HUB[0] + math.sin(a) * 9.0, HUB[1] - math.cos(a) * 9.0, RIM_Z),
          (0.45, 0.45, 0.45) if lamp else (0.3, 0.3, 0.3), "#ffe2a8" if lamp else BRASS_DARK,
          solid=False, material={"roughness": 0.3, "metallic": 0} if lamp else BRASS_MATERIAL)
for spoke in range(8):
    a = spoke * math.tau / 8 + math.tau / 16
    for k in range(1, 12):
        d = k * 0.75
        shape("box", (HUB[0] + math.sin(a) * d, HUB[1] - math.cos(a) * d, RIM_Z), (0.28, 0.28, 0.2),
              IRON, solid=False, material=IRON_MATERIAL)
GONDOLAS = 6
for i in range(GONDOLAS):
    color = BRASS if i % 2 == 0 else COPPER
    parts = [make("box", (0, -0.2, 0), (3.0, 0.4, 2.4), color, material=BRASS_MATERIAL)]
    for z in (-1.15, 1.15):  # Hanger posts at the ends, drawn only.
        parts.append(make("box", (0, 0.9, z), (0.12, 1.8, 0.12), IRON, solid=False,
                          material=IRON_MATERIAL))
    parts.append(make("box", (0, 1.85, 0), (0.2, 0.12, 2.5), IRON, solid=False,
                      material=IRON_MATERIAL))
    mover(f"Gondola {i + 1}", HUB, parts, orbit={"radius": 9.0, "upright": True}, period=30.0,
          phase=i / GONDOLAS)
# The deck at the bottom and the sky walk at the top sit beside the wheel, never in its way.
deck(36, 44, -34, -30, 3.0, STONE)
shape("ramp", (40, 1.5, -38.5), (8, 3, 9), STONE_DARK, yaw=math.pi,
      material={"roughness": 0.75, "metallic": 0})
checkpoint(42.5, 3.0, -32, math.pi / 2)
spark("Halfway round", HUB[0] + 9.0, HUB[1] + 1.1, HUB[2])
spark("Top of the wheel", HUB[0], HUB[1] + 9.0 + 2.9, HUB[2])
light((HUB[0], HUB[1], HUB[2] + 2.0), (1.0, 0.8, 0.5), 20, 14)
SKY = 21.0
deck(30, 44, -34, -30, SKY, STONE)
for x in (31, 43):
    shape("cylinder", (x, (SKY - 0.6) * 0.5, -33), (0.9, SKY - 0.6, 0.9), STONE_DARK,
          material={"roughness": 0.75, "metallic": 0})
deck(TX + 9, 14, -34, -30, SKY, STONE)
mover("Sky ferry", (27.5, SKY, -32), slab_parts(4, 4), path=[[0, 0, 0], [-11, 0, 0]],
      period=10.0, pause=1.5)
spark("Sky ferry", 22.0, SKY + 1.6, -32)

# --- Northwest: the carousel -------------------------------------------------------------

CAROUSEL = (-35.0, 0.8, -35.0)
zones.append({"name": "Carousel", "center": [CAROUSEL[0], 0, CAROUSEL[2]], "radius": 14})
parts = [make("cylinder", (0, -0.3, 0), (20, 0.6, 20), CANOPY_CREAM,
              material={"roughness": 0.6, "metallic": 0}),
         make("cylinder", (0, 2.5, 0), (2.4, 5.0, 2.4), BRASS, material=BRASS_MATERIAL),
         make("cone", (0, 9.0, 0), (16, 2.4, 16), CANOPY_RED, taper=0.08, solid=False,
              material={"roughness": 0.7, "metallic": 0})]
PLINTHS = [(1.2, 0.0), (2.2, 0.72), (3.2, 1.44), (4.2, 2.16)]  # height, angle
for height, angle in PLINTHS:
    parts.append(make("cylinder", (math.sin(angle) * 7, height * 0.5, -math.cos(angle) * 7),
                      (2.0, height, 2.0), BRASS if height < 4 else COPPER, material=BRASS_MATERIAL))
for i in range(6):  # Horses on poles, drawn only.
    a = i * math.tau / 6 + 0.36
    hx, hz = math.sin(a) * 4.4, -math.cos(a) * 4.4
    parts.append(make("cylinder", (hx, 3.9, hz), (0.12, 7.8, 0.12), BRASS, solid=False,
                      material=BRASS_MATERIAL))
    parts.append(make("ellipsoid", (hx, 1.6, hz), (0.6, 0.9, 1.6), HORSE, yaw=a + math.pi / 2,
                      solid=False, material={"roughness": 0.5, "metallic": 0}))
    parts.append(make("ellipsoid", (hx + math.sin(a + math.pi / 2) * 0.7, 2.2,
                                    hz - math.cos(a + math.pi / 2) * 0.7), (0.4, 0.6, 0.6), HORSE,
                      solid=False, material={"roughness": 0.5, "metallic": 0}))
mover("Carousel", CAROUSEL, parts, spin=0.25)
top_height, top_angle = PLINTHS[-1]
spark("Round and round", CAROUSEL[0] + 8.6, CAROUSEL[1] + 1.1, CAROUSEL[2])
# The tallest plinth's spark turns with it; a spark cannot ride, so it waits where the
# carousel starts it and the plinth comes back round to it every 25 seconds.
spark("Brass horse", CAROUSEL[0] + math.sin(top_angle) * 7, CAROUSEL[1] + top_height + 1.2,
      CAROUSEL[2] - math.cos(top_angle) * 7)
light(CAROUSEL[:1] + (6.5,) + CAROUSEL[2:], (1.0, 0.65, 0.5), 18, 13, orbit_radius=5.0,
      orbit_speed=0.25)

# --- Lamps along the walks ---------------------------------------------------------------

for x, z in ((-3, 30), (3, 18), (-3, 0), (3, -14), (-14, 18), (16, 18)):
    lamp_post(x, z)

level = {
    "version": 1,
    "name": "clockwork-garden",
    "title": "The Clockwork Garden",
    "kill_height": -10,
    "atmosphere": {"time_of_day": 17.5, "fog": 0.8, "grid": False},
    "shapes": shapes,
    "movers": movers,
    "checkpoints": checkpoints,
    "collectibles": sparks,
    "lights": lights,
    "zones": zones,
    "portals": portals,
}
assert len(lights) <= 16, len(lights)
assert len(sparks) == 16, len(sparks)


def dump(value, indent=""):
    """One shape, mover or entry per line keeps diffs readable."""
    lines = ["{"]
    keys = list(value)
    for i, key in enumerate(keys):
        item = value[key]
        comma = "," if i + 1 < len(keys) else ""
        if isinstance(item, list) and item and isinstance(item[0], dict):
            lines.append(f'  "{key}": [')
            for j, entry in enumerate(item):
                tail = "," if j + 1 < len(item) else ""
                lines.append("    " + json.dumps(entry, separators=(", ", ": ")) + tail)
            lines.append("  ]" + comma)
        else:
            lines.append(f'  "{key}": ' + json.dumps(item) + comma)
    lines.append("}")
    return "\n".join(lines) + "\n"


out = Path(__file__).resolve().parents[2] / "assets" / "levels" / "clockwork-garden.json"
out.write_text(dump(level))
print(f"{out}: {len(shapes)} shapes, {len(movers)} movers, {len(sparks)} sparks, "
      f"{len(checkpoints)} checkpoints, {len(lights)} lights")
