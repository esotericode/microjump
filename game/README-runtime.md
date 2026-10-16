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

**Speed is earned.** Long jumps and running downhill make you faster than a run. Jump
again right as you land (the overlay shows "Clean") to keep that speed. Jumping early,
mashing, turning hard or running uphill bleeds it away. Chained clean long jumps reach
top speed.

## Extras

```text
microjump --record run.mjr          record your input; replay with --replay run.mjr
microjump --time-of-day 21          start at 21:00
microjump --shadow-quality high     low, medium or high
microjump --level other.json        load another level from assets/levels/
microjump --pacing-test --frame-log pacing.csv   if motion ever stops feeling smooth
```
