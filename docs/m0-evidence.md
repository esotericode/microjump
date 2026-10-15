# M0 foundation evidence — 2026-10-15

Branch: `codex/m0-foundation`. Base: `34b498079a2787c5d40cac5a70728cc953692e60`.
M0 remains **in progress** until Windows CI and owner Windows startup are verified.
No renderer, gameplay, frame capture, FPS readout or performance results are claimed.

## Implementation

- C++23 static engine library and playground executable with public engine headers.
- Ninja presets for GCC Debug/Release, Clang Debug and MSVC Debug/Release.
- Commit/hash-pinned SDL 3.4.16, volk SDK 1.4.341.0 and doctest 2.4.12; no other
  runtime dependencies fetched. SDK/driver comes from the platform.
- SDL3 resizable/high-density window and event handling; Escape/close exits.
- Vulkan 1.3 instance/surface, device capability checks, graphics/presentation queue,
  and enabled dynamic rendering, synchronization2 and timeline semaphore features.
- Debug validation is mandatory, including instance creation and resource destruction.
  Validation errors make the executable fail. Partial initialization uses RAII cleanup.
- CI builds/tests on Linux and Windows, checks formatting/tidy, and packages Release
  zips with SDL shared library and third-party notices. Windows runtime is an owner check.

## Local verification

Ubuntu 26.04, GCC 15.2, Clang 21.1.8, CMake 4.2.3, Ninja 1.13.2,
Vulkan headers/validation 1.4.341, Intel(R) Graphics (MTL), native Wayland session.
Tools were downloaded from Ubuntu repositories and extracted into `.local-tools/`;
sudo required interactive authentication. No drivers or system settings were changed.

For each `PRESET` in `linux-debug`, `linux-clang-debug`, `linux-release`:

```sh
tools/local-env.sh cmake --preset PRESET -DMJ_SMOKE_TESTS=ON
tools/local-env.sh cmake --build --preset PRESET --parallel 4
tools/local-env.sh ctest --preset PRESET -V
```

All three builds passed with project warnings treated as errors and no compiler
warnings. Each suite passed **4/4 tests**: 2 doctest cases / 17 assertions covering CLI
parsing and every required device capability, CLI help, rejection of unknown flags,
and actual Vulkan window/device startup/shutdown. Debug builds used ASan + UBSan.
The missing-validation-layer and unavailable-SDL-driver paths were also exercised:
both exited with code 1 and a clear diagnostic, without a sanitizer error.

Observed debug startup:

```text
Vulkan validation: enabled (errors fail this run)
Vulkan device: Intel(R) Graphics (MTL)
Vulkan 1.3 device ready: dynamic rendering, synchronization2, timeline semaphores enabled
M0 ready: window + Vulkan device; rendering begins in M1. Escape closes the window.
Shutdown complete; Vulkan validation errors: 0
```

Formatting and all four implementation translation units passed:

```sh
find engine game tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 tools/local-env.sh clang-format --dry-run --Werror
find engine/src game -name '*.cpp' -print0 | xargs -0 -n 1 tools/local-env.sh clang-tidy -p build/linux-clang-debug
```

No project diagnostics; tidy's informational suppressed-warning counts refer to
non-project headers. C++ module scanning is explicitly off, consistent with the charter.

Packaging passed:

```sh
tools/local-env.sh cpack --config build/linux-release/CPackConfig.cmake -B build/packages
```

The zip contains the executable, `libSDL3.so.0`/versioned library, README, project
license, SDL/volk/doctest license notices. Extracted with CMake/libarchive to preserve
symlinks, then launched with `LD_LIBRARY_PATH` unset: successful Intel MTL startup,
exit code 0. `readelf -d` confirms only `$ORIGIN` in the installed runtime search path.

## Issues encountered and their scope

- The system libdecor GTK plugin retains Fontconfig allocations after SDL shutdown.
  The first startup passed Vulkan validation but failed LSan: 320 bytes / 3 allocations
  in `libfontconfig.so.1`. Removing the inherited `GDK_BACKEND=x11` made GTK initialize
  and exposed its larger font cache, rather than fixing the lifetime report.
  `tests/lsan.supp` suppresses that library only for the startup test/local tool wrapper.
  ASan/UBSan and other leak checks remain enabled. The inherited desktop environment
  can also print `libdecor-gtk-WARNING: Failed to initialize GTK` and fall back to an
  undecorated window; this does not prevent Vulkan initialization. Revisit the plugin
  behavior when checking the first rendered window on the owner's desktop.
- doctest 2.4.12 includes `<ciso646>` under Clang. GCC 15's standard headers warn
  that it was retired in C++20. The Clang suppression is confined to the doctest
  include; project warnings remain errors. No upstream sources are modified.
- Only combined graphics/present queue families are supported in M0. Devices without
  one get a clear error. Selection does not claim support for split queue ownership.
- This zip uses the host's system C/C++ runtime; it is not a portable Linux distribution
  bundle. SDL and required notices are included.

## Remaining gates / next increment

- Confirm all PR CI jobs, including MSVC Debug/Release, pass; record the artifact link.
- The owner authorized publishing and merging this branch on 2026-10-15 and
  requested standing agent push/merge authority. Charter v0.3 / D-020 records it.
- Owner: extract the Windows Release zip and run `microjump.exe --smoke-test`, then
  normal startup, resize and close. Confirm successful Vulkan startup on that PC.
- M1: swapchain + dynamic rendering, project math/transforms, original 3D geometry,
  debug camera, fixed tick/interpolation, frame capture and FPS readout. Screenshots
  begin with rendered pixels; the empty M0 window has no visual rendering evidence.
