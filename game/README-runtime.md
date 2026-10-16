# microjump — JSON playground and lighting

Extract the complete zip, including `assets/`, `shaders/` and `licenses/`.
Run `microjump.exe` on Windows or `./microjump` on Linux. A Vulkan 1.3 GPU driver
is required. Release builds do not need the Vulkan SDK or development tools.

- W/A/S/D or the left stick: move. Space or A: jump (hold for a higher jump).
  Ctrl: walk. Mouse or the right stick: orbit the camera.
- R, Home or Back: respawn at the last checkpoint pad you stood on.
- F1: diagnostics overlay (FPS/frame time, resolution, VSync/cap, movement state).
- F2 or Start: live tuning window (frees the mouse cursor): time of day and day/night
  animation, shadow quality and biases, lights, movement and camera values.
- F3: free-flight camera (WASD/QE, Shift faster).
- Escape or the close button exits.

Record and replay your input (the replay exits when it ends and prints the final state):

```text
microjump.exe --record run.mjr
microjump.exe --replay run.mjr
```

To capture the 12th rendered GPU frame and exit:

```text
microjump.exe --capture-frame 12 --capture-out frame.png
```

On Linux use `./microjump` instead. `--frames 60` renders 60 frames and exits;
`--smoke-test` renders 12. PNGs include the overlay when visible and use the
actual pixel resolution, including display scaling. Keep `assets/` and `shaders/` beside
the executable; running from another working directory is supported.

Lighting options:

```text
microjump.exe --time-of-day 21          (start at 21:00; 12 is noon)
microjump.exe --shadow-quality high     (low, medium or high)
```

The default playground spans 230 m. Head west for four platform routes, north along
the marked sprint lane, or south-east for the summit stairs and lighting pavilion.
Checkpoints are teal pads. Street lamps switch on at dusk. The F1 overlay names the
current region. Wall jumps and a momentum move are still being implemented.

Levels are editable JSON files in `assets/levels/`. Use `--level PATH` to select one;
the default is `playground.json` beside the executable. Old M2/M3 recordings use
`--level assets/levels/movement-yard.json`. Replays require the same level and tuning.
Display-setting controls will follow. VSync is FIFO On, with no application FPS cap.
