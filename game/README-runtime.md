# microjump — first scene

Extract the complete zip, including the `shaders/` and `licenses/` folders.
Run `microjump.exe` on Windows or `./microjump` on Linux. A Vulkan 1.3 GPU driver
is required. Release builds do not need the Vulkan SDK or development tools.

- W/A/S/D: fly forward/left/back/right. Q/E: down/up. Shift: faster.
- Hold the right mouse button to look. Home resets the camera.
- F1 toggles FPS/frame time, pixel resolution and VSync/cap readouts.
- Escape or the close button exits.

To capture the 12th rendered GPU frame and exit:

```text
microjump.exe --capture-frame 12 --capture-out frame.png
```

On Linux use `./microjump` instead. `--frames 60` renders 60 frames and exits;
`--smoke-test` renders 12. PNGs include the overlay when visible and use the
actual pixel resolution, including display scaling. Keep `shaders/` beside
the executable; running from another working directory is supported.

This is a small renderer/camera scene. Character movement, collisions, dynamic
shadows and the full playground will follow. VSync is currently FIFO On, with
no application FPS cap; display-setting controls come in a later milestone.
