# microjump — movement and lighting slice

Extract the complete zip, including the `shaders/` and `licenses/` folders.
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
actual pixel resolution, including display scaling. Keep `shaders/` beside
the executable; running from another working directory is supported.

Lighting options:

```text
microjump.exe --time-of-day 21          (start at 21:00; 12 is noon)
microjump.exe --shadow-quality high     (low, medium or high)
```

The lighting pavilion is behind and to the right of the starting point (south-east). Street
lamps switch on at dusk. Wall jumps, the full playground and display-setting controls
will follow. VSync is currently FIFO On, with no application FPS cap.
