# Gamepad support

2026-10-16. This completes and tests the earlier basic SDL input path. No new
library, controller database, physics or movement dependency is introduced.

Connect a controller that SDL recognizes, over the connection supported by its
SDL/OS driver. Already connected controllers are discovered at startup. The first
available pad controls the player; keyboard/mouse remain usable. If it disconnects,
the next connected pad takes over; reconnecting works without restarting. The
overlay reports the selected controller's name and detected jump-button label.
Unmapped raw joysticks are outside this increment.

| Control | Action |
| --- | --- |
| Left stick | Analog camera-relative movement; partial tilt walks. |
| Right stick | Camera orbit. |
| South face button: A / Cross / B | Jump / wall kick; hold for height. |
| Left shoulder + jump | Running long jump. |
| Right shoulder | Walk modifier. |
| Right-stick click (R3) | Center camera behind the player at the default pitch. |
| Back / Select / Create | Respawn at the active checkpoint. |
| Start / Options / Menu | Open or close tuning. |
| North face button: Y / Triangle / X | Toggle diagnostics. |

In tuning, use the D-pad or left stick to navigate, South to activate a control,
then left/right to adjust a selected slider. South commits the edit; East backs
out. Start closes the window. Gameplay inputs are neutral while the menu is open;
simulation, gravity and lighting continue. Controller UI navigation is disabled
while the game window lacks focus.

F2 / Start → **Gamepad** exposes separate radial move/look dead zones (default
0.18), rescaled so full tilt still reaches full input. **Camera** exposes stick
yaw/pitch speed and inverted Y. These remain session tunables, matching the
existing movement/camera tuning; saving settings is still M5.

## Reliability changes

The SDL gamepad owner now lives in `engine/platform/gamepad.*`, independently
of window/video setup, so tests can use actual SDL virtual devices headlessly.
It enumerates controllers at startup and on connection changes, preserves the
current selection while other pads connect, and clears queued edges when that
selection is removed. Inputs from inactive controllers cannot trigger actions.

Jump presses and the shoulder state at each press are latched in event order.
A complete shoulder+jump tap between polls therefore remains a long-jump request,
even when both buttons are released by the time held state is sampled. A held
button does not generate repeated press edges. Losing focus consumes pending
edges and returns neutral controls; main also clears edges awaiting a fixed tick.

The existing version-2 replay format is unchanged. Processed stick values,
buttons and camera orientation still feed the 120 Hz tick stream. A camera
recenter is represented by its resulting recorded orientation, so replay needs
no additional button bit. The movement/camera simulation remains project-owned.

## Verification

Six new headless tests use SDL virtual gamepads, rather than injecting fake game
button masks. They cover startup detection, analog directions and diagonal
normalization, drift/dead-zone tuning, held and short-tap input, shoulder+jump,
walk/recenter/reset/menu/overlay buttons, focus loss, unplug/reconnect, ignoring
an inactive second controller, and takeover by an already connected backup.
Invalid dead-zone values stay finite and bounded.

A separate display/Vulkan test drives a virtual pad through the production
`Window::poll_events`, gameplay/camera and the actual ImGui tuning window. It
runs, long jumps, recenters, respawns, opens the menu, selects and changes the
time-of-day slider, closes it and jumps again. It checks that the player stays
still while tuning is open (the window withholds controller gameplay input while the
menu has it; keyboard movement stays available) and replays its recorded stream to the identical
final position. The engine captures the menu from the GPU. The fixture presents
the Wayland window before waiting for real focus; it never synthesizes focus or
bypasses the background-input guard.

Only the test processes disable physical joystick drivers, using process-local
SDL hints, to isolate the virtual devices. Normal builds retain SDL's drivers.
The display fixture is enabled by `MJ_SMOKE_TESTS`; Windows CI runs the headless
virtual-device tests without requiring a GPU.

```sh
tools/local-env.sh cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON
tools/local-env.sh cmake --build --preset linux-debug --parallel 4
tools/local-env.sh ctest --preset linux-debug --output-on-failure
# Isolated device tests:
tools/local-env.sh ./build/linux-debug/tests/microjump_tests --test-case='*gamepad*,radial*'
# Production window + gameplay + tuning, with GPU capture:
tools/local-env.sh ctest --preset linux-debug -R '^gamepad_window$' -V
```

## Local evidence

All **22 CTests** passed on native Wayland/Intel Vulkan with GCC Debug and on
Xvfb/lavapipe with GCC Release and Clang Debug. The final headless suite has
**52 cases / 89,881 assertions**; the final poll-boundary removal regression was
rerun separately on all three builds. The rendered controller/menu fixture also
passes natively and with software Vulkan. ASan/UBSan and Vulkan synchronization
validation are enabled in Debug; GPU tests report zero validation errors.
Formatting, diff checks and clang-tidy pass.

The updated keyboard/mouse input smoke test also records and exits cleanly,
confirming those controls remain usable alongside the controller changes.
[Inspected GPU capture of controller-operated tuning](captures/gamepad-menu.png).
The image uses a virtual controller and the original yard as a focused test
fixture, rather than claiming a physical controller session. The changed
night-time view comes from controller editing of the actual time-of-day slider.

## Remaining hardware acceptance

Virtual SDL tests verify our input path and UI integration. They do not establish
USB/Bluetooth transport reliability, physical stick calibration, real controller
button labels, or Windows GPU behavior. No physical controller was available to
the agent. The owner should test their controller; no vibration, remapping UI,
or saved controller profiles are claimed. The debug free-flight camera remains
a keyboard/mouse tool.

```text
PLAYTEST REQUEST
Build/branch: codex/gamepad-support (ready-to-run Windows zip in the PR)
How to run:   microjump.exe; Linux: tools/local-env.sh ./build/linux-release/game/microjump
Try:          Connect before launch and during play. Move/look, hold/release jump,
              use left shoulder + jump at speed, and alternate kicks on purple walls.
              Click the right stick, respawn, open Start, adjust a slider and close it.
              Unplug/reconnect; if available, leave a second pad connected and unplug
              the active one. Switch focus away while holding a button, then return.
Look for:     Correct controller name/button label, stick drift, comfortable camera
              speed, no unintended jumps after focus/menu changes, clean handoff.
Tunables:     Start/F2 > Gamepad dead zones; Camera stick speeds and Invert Y.
```
