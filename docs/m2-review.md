# M2 review and shutdown-failure investigation

2026-10-15. Reviewed the M1-to-M2 changes through `a460562`, including PRs
[#5](https://github.com/esotericode/microjump/pull/5) and
[#6](https://github.com/esotericode/microjump/pull/6). This follow-up keeps M2 in
progress pending owner playtesting and Windows GPU acceptance.

## What Claude's reported failure actually was

The original test output was recoverable from the prior agent's local tool log.
`render_capture` finished rendering and printed:

```text
GPU frame captured as PNG
Rendered frames: 12
Shutdown complete; Vulkan validation errors: 0
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 96 byte(s) in 1 object(s)
```

Its allocation stack passes through `wl_proxy_marshal_array_flags` /
`wl_proxy_marshal_flags` in system `libwayland-client.so.0`, three frames in
`libdecor-gtk.so`, `libdecor_frame_commit`, and SDL's
`decoration_frame_configure`. This supports an external Wayland decoration-plugin
leak reported at process exit. The recovered log shows a failed sanitizer check,
not a segmentation fault during play. It does not establish the plugin's internal
ownership mistake, nor demonstrate that the upstream leak has been repaired.

Claude's `61733ed` adds `leak:libdecor-gtk.so` to `tests/lsan.supp` (D-024).
That prevents allocations with plugin frames from failing the leak check; it
does **not** fix the plugin itself. ASan/UBSan and other leak checks remain
enabled. The prior agent recorded 24 passing native GPU test runs afterward.
Hosted X11 CI doesn't load this Wayland plugin.

During this review, the unchanged M2 branch passed all 10 original CTests on
native Intel Vulkan. One attempt with the old suppression also passed, but GTK
decoration initialization failed and fell back to no decorations, so that run
does not reproduce or refute the original intermittent plugin leak.

## Bugs confirmed and fixed in this review

| Trigger | Previous behavior | Correction and regression evidence |
| --- | --- | --- |
| One valid replay tick followed by a run of `18446744073709551615` ticks | Unsigned addition wrapped the four-hour limit, then `vector::insert` threw an uncaught `std::length_error`. The old Release executable aborted with exit 134. | Compare the run against the remaining limit using subtraction. Parser and real CLI tests require rejection. The fixed executable returns 1 with `Replay line 5: replay longer than four hours` for the committed fixture. |
| Replay recorded before the first simulation tick | An empty vector selected live-input mode, so playback kept running. | Determine playback mode from `--replay`, independently of tick count. GPU integration test requires completion at tick 0. |
| Set coyote time or jump buffer to zero in F2 | Ordinary grounded jumps stopped working, because both positive timers were required. | Check the current press and grounded state directly, with timers supplying only the optional grace periods. Tests cover all four zero/default combinations. |
| Orbit beside a wall less than 0.9 m from the camera focus | The minimum camera distance overrode the obstruction sweep and put the camera through the wall. | Collision takes precedence over the preferred distance. Tests verify probe clearance at three interpolation fractions. |
| Set ground snap to zero | A zero-length downward sweep lost floor contact even while standing on flat ground. | Retain the short collision-skin contact probe when ledge snapping is disabled. A 20-tick walking test checks continuous ground contact and stable height. |
| Replay logs success, then exits nonzero during shutdown | CTest's `PASS_REGULAR_EXPRESSION` ignored the exit code, potentially hiding a LeakSanitizer failure. | The replay-check script requires both exit 0 and the expected completion / zero-validation messages. A process deliberately printing the success text and exiting 1 was correctly rejected. |

The four new unit regressions failed on the original implementation, including
the replay exception. After the fixes, all 26 doctest cases / 4,828 assertions pass.
Default movement and the committed 439-tick replay retain their expected outcomes.

## Verification

Commands use the existing rootless tools, with `MJ_SMOKE_TESTS=ON`:

```sh
tools/local-env.sh cmake --build --preset PRESET --parallel 4
tools/local-env.sh ctest --preset PRESET -V
```

The review PR records final Linux/MSVC CI results and runnable build downloads.
No performance measurements or driver/system changes are part of this review.

Local results: all **12 CTests** pass in GCC Debug and GCC Release on native
Wayland / Intel Vulkan, and Clang Debug under Xvfb / software Vulkan. Both Debug
builds enable ASan/UBSan and Vulkan synchronization validation; all GPU tests
reported zero validation errors. All configurations pass the 26 doctest cases /
4,828 assertions. Formatting, `git diff --check`, and clang-tidy pass. Fresh
spawn, movement-replay and resize PNGs were decoded by the tests; spawn and
movement-replay captures were also visually inspected.

The previously reported one-off Wayland resize-capture mismatch remains an open
test-fixture concern in `m2-movement.md`; its cause has not been demonstrated.
Physical gamepad input, movement feel and Windows GPU behavior still need owner
testing. These are separate from the historical shutdown leak.
