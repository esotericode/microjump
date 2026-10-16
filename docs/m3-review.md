# M3 review

2026-10-15: reviewed the lighting changes through `a2f0bd3` (PR #7), including
resource lifetimes, shadow/HDR synchronization, uniform/shader layout, linear-light
output, day/night fixtures, packaging rules and the input/capture changes.

The unchanged implementation passed all 16 native Intel CTests. Fresh day and
night-pavilion GPU captures were inspected; the scene, material samples, moving
lights and shadows are present. This is functional verification, not a benchmark
or owner approval of the visual defaults.

One confirmed issue: `--capture-tick 1` advanced to tick 3 on a frame that processed
several fixed ticks. The interpolated player pose and light clock also depended on
the remainder of the render accumulator. It could not reliably capture the same
moment across render cadences.

The loop now stops simulation advancement at the requested capture tick, renders
that state without interpolation, and uses the matching light-animation time.
Tick capture also overrides implicit smoke/camera frame limits, and a requested
tick beyond a replay's length fails before opening a window. A capture logs its
simulation state for reproducibility. The existing clean-exit replay checker now
checks an exact tick-1 capture; it failed before the fix and passes afterward.

After the fix: all 17 native Intel CTests pass, including 32 doctest cases /
49,824 assertions, ASan/UBSan and Vulkan synchronization validation. Every GPU
test reported zero validation errors. CI results and runnable builds are in PR #7.

The prior owner gates remain: M2 movement/camera playtest, M3 visual review,
physical gamepad and Windows GPU checks. Single-map shadow coverage, unshadowed
local lights, the near-obstacle camera feel and the historical intermittent
Wayland resize fixture remain documented in `m3-lighting.md`.
