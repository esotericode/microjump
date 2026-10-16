# microjump

Keep the whole folder together: the game needs `assets/` and `shaders/` beside it. Run
`microjump.exe` on Windows or `./microjump` on Linux. You only need a GPU driver with
Vulkan 1.3; nothing else to install.

## Controls

| Keyboard / mouse | Gamepad | Action |
| --- | --- | --- |
| W A S D | Left stick | Move; a partial tilt walks |
| Space | South button | Jump (hold for height) |
| Space again at a purple wall | South button | Wall kick; alternate walls to climb |
| Shift + Space while running | Left shoulder + jump | Long jump |
| Space again as you land | South again as you land | Double jump; once more while moving: triple jump |
| E or left click | West button (X, Square, Y) | Dive; jump while sliding to roll out |
| Ctrl | Right shoulder | Walk |
| Mouse | Right stick | Camera; click the right stick to recenter |
| R | Back | Back to the last checkpoint (teal pads) |
| F1 | North button | Diagnostics overlay |
| F2 | Start | Tuning: movement, camera, lighting, gamepad |
| F3 | — | Free-flight camera |
| Escape | — | Quit |

On Windows, pressing Shift five times can open the Sticky Keys prompt. You can turn
that shortcut off in Settings › Accessibility › Keyboard.

## Where to go

From the plaza:

- **West:** the platform routes.
- **North:** the sprint lane, the place for long jumps.
- **East:** the purple wall-kick towers.
- **South-east:** the summit stairs and the lighting pavilion.

Street lamps come on at dusk. F2 lets you change the time of day.

**Somewhere else:** the glowing ring just right of spawn is a gate to **the Lantern
Isles**, floating islands above the clouds at golden hour. Hop the stepping stones to
the blossom grove and climb its canopies, kick up the crystal spire, climb the ruins'
columns, or run the long pier and long-jump the gap at its end. Echo Plateau's Three
Steps teach the triple jump, pink mushrooms throw you skyward, and four secrets hide
around the isles: look under edges, behind trees and out from high places. Sixteen
sparks wait there. The gate on the first island brings you home, and each place keeps its sparks
until you quit.

**Follow the golden sparks.** There are 16 to find. The first waits just ahead of
spawn; others reward the plaza crown, small stairs and ramp, the winding platform
route, each narrowing western route, both wall decks, the summit, sprint lane and a
quiet corner in the pavilion. Walk or jump into them. The **SPARKS** counter shows
progress, names each find and celebrates when you have them all. Checkpoint respawns
keep your finds; restarting the game begins a fresh hunt. F1 leaves this counter
visible; `--hide-overlay` hides it along with diagnostics for clean captures.

**Speed is earned.** Long jumps and running downhill make you faster than a run. Jump
again right as you land (the overlay shows "Clean") to keep that speed. Jumping early,
mashing, turning hard or running uphill bleeds it away. Chained clean long jumps reach
top speed.

Dust follows skids and hard landings. Golden motes mark clean momentum hops and spark
finds, so those successes stay visible with F1 diagnostics closed.

## Extras

```text
microjump --record run.mjr          record your input; replay with --replay run.mjr
microjump --time-of-day 21          start at 21:00
microjump --shadow-quality high     low, medium or high
microjump --level other.json        load another level from assets/levels/
microjump --pacing-test --frame-log pacing.csv   if motion ever stops feeling smooth
```
