# Movement: momentum you build on purpose

2026-10-16. Follows the [M4 wall/long-jump increment](m4-moves.md) and a review finding:
after one long jump, timing ordinary jumps for the moment you landed kept 16.2 m/s
(1.8× run speed) forever. They also jumped higher than designed (2.95 m instead of
2.58 m), because the speed bonus to jump height had no limit. The jump buffer made
this effortless, since any press during the last 0.12 s before landing counted.

Owner direction: *building speed and maintaining it should be a thing, but not
accidental.* This note records what we took from games with well-regarded movement,
the resulting model, and what remains for playtesting.

## What we learned from other games

These are design observations from public write-ups, guides and play. No source code
was used (clean-room rule, Section 3). For Super Mario 64 only behavior descriptions
were consulted.

- **Super Mario 64:** the long jump is a fast, low, committed arc that is quicker than
  running. Its best-known speed exploit, the backwards long jump, stacks speed on
  every jump with no ceiling ([SDA knowledge base](https://kb.speeddemosarchive.com/Super_Mario_64)).
  - *Lesson:* a momentum move needs a hard ceiling.
  - *Lesson:* the special moves should have fixed, readable arcs.
- **Titanfall 2 slide hopping:** speed is kept by jumping as you touch the ground,
  and ground friction takes it away when you mistime the rhythm
  ([Titanfall wiki](https://titanfall.wiki.gg/wiki/Slide_Hop),
  [player discussion](https://steamcommunity.com/app/1237970/discussions/0/4298195085740384735)).
  Steering abruptly loses speed while flowing turns keep it.
  - *Lesson:* timing at landing is the skill, and friction is the cost of missing it.
- **Quake / Source bunny hopping** (general knowledge of these games): momentum
  survives only when the jump happens before ground friction is applied.
  - *Lesson:* with an automatic buffered jump the hop is never missed, so the
    buffer must not grant the reward by itself.
- **Celeste:** coyote time, jump buffering and reduced gravity at the apex while jump
  is held make the controls forgiving without lowering the skill ceiling
  ([Celeste & Forgiveness](https://maddymakesgames.com/articles/celeste_and_forgiveness/)).
  - *Lesson:* keep the forgiveness, so a buffered press still jumps. Only the
    momentum reward depends on timing.
- **Sonic, Tribes skiing, A Hat in Time** (general knowledge): slopes are a
  terrain-based speed source.
  - *Lesson:* downhill should build speed and uphill should cost it, which makes
    the level a tool for speed.
- **Steve Swink, *Game Feel*, and Kyle Pittman, "Math for Game Programmers: Building a
  Better Jump" (GDC 2016):** derive jump physics from a height and a time to apex, and
  keep the moves predictable.
  - *Lesson:* our jump already works this way.

## The model

**Momentum** is grounded or flying speed above `run_speed`. Running alone never
creates it.

| Source / sink | Rule |
| --- | --- |
| Long jump | Launches at least `run_speed × 1.8` (unchanged first long jump), or current speed `+ long_jump_chain_boost` when chained from momentum, capped at `max_speed`. |
| Downhill running | Holding forward down a walkable slope adds `slope_momentum` × the slope's gravity component. Standing or releasing the stick never slides. |
| Clean hop | A grounded takeoff within `hop_window` of touchdown, on either side, keeps all momentum. Ground friction pauses for that window. |
| Early hop | A buffered press that began more than `hop_window` before touchdown still jumps, but keeps only `early_hop_retention` of the momentum. |
| Mashing | Repeated presses inside the buffer continue one press chain, judged from its first press. Mashing therefore counts as early. |
| Late hop | After the window, flat ground removes `overspeed_friction` m/s every second. |
| Uphill | Adds to friction in proportion to the slope. |
| Turning or easing off | Momentum is only carried while the stick is nearly full and within about 25° of facing. Otherwise the ordinary (stronger) deceleration applies. |
| Air | `air_drag` m/s per second, down to run speed. Air steering keeps the existing limits. |
| Jump height | The speed bonus stops at run speed. Momentum buys distance, not height. |
| Wall kick | Starts from its fixed launch. Momentum does not carry through a kick. |

Ordinary jumps keep their variable height. **Long jumps and wall kicks now have fixed
arcs**: releasing jump no longer cuts them. A tapped wall kick previously rose 0.98 m
against 2.31 m when held, which made climbing depend on an undocumented hold.

Slopes are judged by the triangle's face normal. The rounded capsule contact at a
ledge reports normals tilted up to 30° even on flat ground, and an early version
treated stepping off the 6 cm spawn pad as a downhill. A regression test covers this.

### Default values and measured behavior

All values are F2 sliders (**Momentum**, plus **Long jump chain boost** under Wall /
long jump) and live in `PlayerTuning`.

| Parameter | Default |
| --- | --- |
| `max_speed` | 22 m/s (about 2.4× run speed). |
| `hop_window` | 0.075 s (9 ticks) either side of touchdown. |
| `early_hop_retention` | 0.4 |
| `overspeed_friction` | 14 m/s² |
| `slope_momentum` | 0.4 |
| `air_drag` | 0.6 m/s² |
| `long_jump_chain_boost` | 2.5 m/s |

Takeoff speeds after one long jump from full run speed, measured headlessly. Each
column is the next takeoff:

| Timing | Long jump | Hop 1 | Hop 2 | Hop 3 | Hop 4 | Hop 5 |
| --- | --- | --- | --- | --- | --- | --- |
| Clean ordinary hops | 16.19 | 15.89 | 15.43 | 14.98 | 14.52 | 14.07 |
| Early (pressed 0.1 s before) | 16.19 | 11.75 | 9.92 | 9.18 | 9.00 | 9.00 |
| Mashed every 4 ticks | 16.19 | 11.75 | 9.92 | 9.18 | 9.00 | 9.00 |
| Late (0.2 s on the ground) | 16.19 | 14.14 | 11.93 | 9.73 | 9.00 | 9.00 |
| Clean long-jump chain | 16.19 | 18.39 | 20.59 | 22.00 | 22.00 | 22.00 |
| Early long-jump chain | 16.19 | 16.19 | 16.19 | 16.19 | 16.19 | 16.19 |

- **Clean ordinary hops** slowly fade through air drag. Holding top speed needs
  clean long jumps or a downhill.
- **Running on after landing** (no hop): 15.9 m/s returns to 9 m/s within 0.6 s on
  flat ground.
- **Downhill:** running from a standstill down a 30 m slope reaches 14.0 m/s at
  10°, 17.3 m/s at 20°, 19.3 m/s at 30° and 20.5 m/s at 45°. A first try at 0.6
  gave 15.9 m/s even at 10°, which was too strong; our jump gravity is about three
  times real gravity. The playground's ramps are much shorter, so their gain is
  modest.

The overlay (F1) shows `Momentum +x.x m/s` and the last momentum takeoff (`Clean`,
`Early`, `Late`), so the timing rule can be learned instead of guessed.

## Unchanged behavior

- **Ordinary movement:** running, turning, skidding, ordinary jumps up to run speed,
  coyote time, buffering and wall-kick rules are unchanged.
- **Committed M2 recording:** the real-input `m2-input-smoke.mjr` still reaches its
  recorded final state, matching it to the last decimal place shown.
- **M4 tour:** the scripted `m4-moves-tour.mjr` was regenerated. Its spawn-to-sprint
  section and its whole wall climb are the original inputs. Air drag makes the long
  jump land 8 cm shorter, so a closed-loop segment now steers back to the original
  wall approach. It finishes at tick 2899 on checkpoint 10, within 4 mm of the old
  finish position. The M4 capture images were taken with the earlier route.

## Verification

`tests/momentum_tests.cpp` covers:

- clean, early, mashed and late hops;
- clean versus sloppy long-jump chains and the cap;
- equal ordinary jump height at momentum and at run speed;
- fixed long-jump and wall-kick arcs alongside a still-variable ordinary jump;
- downhill gain, friction back to run speed, no sliding while idle, and uphill
  draining more than flat ground;
- the pad-edge slope regression.

The existing long-jump test now checks the chain boost.

`tests/replays/m4-momentum-chain.mjr` is a **scripted** recording on the real
playground. It walks to the sprint lane, runs north and chains four long jumps, each
pressed 25 ms before touchdown: 16.2 → 18.4 → 20.6 → 22 → 22 m/s, every takeoff
judged Clean, no respawns. A unit test replays it. To watch it:

```sh
tools/local-env.sh ./build/linux-release/game/microjump --replay tests/replays/m4-momentum-chain.mjr
```

![Third chained long jump at 21.97 m/s with the momentum readout (native GPU capture, tick 552)](captures/momentum-chain.png)

## Next refinements (candidates, not yet done)

- **Apex hang:** reduced gravity near the top of an ordinary jump while held
  (Celeste). This changes every jump height, so it needs owner playtesting first.
- **Visible timing feedback** on the player, beyond the F1 overlay: a brief flash or
  sound on a clean hop. Audio waits until M4 is complete (D-014).
- **A momentum line in the playground:** a long downhill into a long-jump gap that
  needs carried speed, with a forgiving bypass.
- **A dedicated slide or roll** that converts slopes into speed (A Hat in Time dive,
  Odyssey roll). This is a new verb, so it needs an owner decision.
- **Tuning while playing:** F2 currently freezes all gameplay input, which makes
  iterating on these values slower.

```text
PLAYTEST REQUEST
Build/branch: claude/momentum-movement
How to run:   tools/local-env.sh ./build/linux-release/game/microjump   (F1 overlay on)
Try:          Sprint lane: Shift+Space long jump, then keep hopping. First press jump
              just as you land, then deliberately early, then mash. Chain clean long
              jumps until the momentum readout stops rising. Run down the slope course
              ramps and compare speed at the bottom; turn sharply at speed.
              Wall kicks: tap jump instead of holding it.
Look for:     Does the clean window feel learnable (not random, not trivial)? Is the
              loss from an early press clear but not punishing? Is 22 m/s fun or too
              fast for the playground? Does slope gain feel natural? Do fixed wall-kick
              and long-jump arcs feel better than variable ones?
Tunables:     F2 > Momentum (max speed, hop window, early hop keeps, ground friction,
              slope momentum, air drag); F2 > Wall / long jump (chain boost).
```
