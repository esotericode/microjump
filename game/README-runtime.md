# microjump — movement playground

Extract the complete zip, including `assets/`, `shaders/` and `licenses/`.
Run `microjump.exe` on Windows or `./microjump` on Linux. A Vulkan 1.3 GPU driver
is required. Release builds do not need the Vulkan SDK or development tools.

- W/A/S/D or left stick: move. Partially tilt the stick to walk, or hold Ctrl / right shoulder.
- Space or the South face button (A / Cross / B): jump; hold for height. Release and
  press again at a purple wall to kick outward; alternate walls to climb.
- Shift + Space or left shoulder + jump: long jump while running.
- Mouse or right stick: orbit the camera. Right-stick click: center it behind the player.
- R, Home or Back / Select / Create: respawn at the last checkpoint pad stood on.
- F1 or the North face button (Y / Triangle / X): toggle diagnostics.
- F2 or Start / Options / Menu: open or close live tuning. Use D-pad / left stick to
  navigate, South to select/edit, left/right to adjust a selected slider, and East to
  back out. Gameplay controls are neutral while tuning is open.
- In tuning, Gamepad adjusts move/look dead zones; Camera adjusts stick speeds and
  inverted Y. Tuning values reset on restart.
- Controllers recognized by SDL connect at startup or during play. Unplugging the
  active pad hands control to another connected pad. The overlay shows its name
  and detected jump-button label.
- F3: keyboard/mouse free-flight camera (WASD/QE, Shift faster).
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
current region. Purple paired walls east of the plaza lead to 6 m and 8 m finish
decks with checkpoint pads. Try running long jumps along the sprint lane.

Speed above a normal run is momentum. Long jumps and running downhill build it.
Jumping again within a moment of landing (the F1 overlay shows "Clean") keeps it.
Pressing too early, mashing, lingering on the ground, turning sharply or running
uphill loses it. Chained clean long jumps build up to the speed cap.

Levels are editable JSON files in `assets/levels/`. Use `--level PATH` to select one;
the default is `playground.json` beside the executable. Old M2/M3 recordings use
`--level assets/levels/movement-yard.json`. Replays require the same level and tuning.
Display-setting controls will follow. VSync is FIFO On, with no application FPS cap.
