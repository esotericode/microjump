# microjump

A custom C++23/Vulkan 1.3 engine, proven through an original 3D movement playground.
Read [AGENTS.md](AGENTS.md) for the authoritative charter, conventions and roadmap.

M0 is under implementation. The executable opens a resizable SDL3 window, creates a
Vulkan instance, presentation surface and compatible device, and shuts down cleanly.
Debug builds require Khronos validation layers and fail on validation errors, including
errors during teardown. **There is no rendered scene or gameplay yet.**

## Linux setup

Ubuntu 26.04; GCC 15 or Clang 21, CMake 3.28+, Ninja, and a Vulkan 1.3 driver:

```sh
sudo apt-get install -y g++ cmake ninja-build clang libclang-rt-21-dev llvm-21 clang-format clang-tidy glslc libvulkan-dev vulkan-validationlayers vulkan-tools mesa-utils pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxext-dev libxrandr-dev libxrender-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-x11-dev libdecor-0-dev
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
./build/linux-debug/game/microjump
```

Use `linux-release` or `linux-clang-debug` in all three commands for the other builds.
Dependencies download on first configure; sources and hashes are pinned. Runtime
dependencies and their licenses are listed in [third_party/README.md](third_party/README.md).

Without sudo, `tools/bootstrap-local.sh` downloads and extracts Ubuntu 26.04 development
packages into ignored `.local-tools/`. It does not modify system packages or drivers.
Prefix the commands above with `tools/local-env.sh`, for example:

```sh
tools/bootstrap-local.sh
tools/local-env.sh cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON
tools/local-env.sh cmake --build --preset linux-debug --parallel 4
tools/local-env.sh ctest --preset linux-debug
tools/local-env.sh ./build/linux-debug/game/microjump --smoke-test
```

The wrapper is required for locally extracted validation layers and shared tool libraries.
It also applies `tests/lsan.supp`, which suppresses only Fontconfig allocations retained
by the system Wayland GTK decoration plugin. CTest applies the same suppression to
the GPU startup test. ASan, UBSan and other leak checks remain active. For a system
toolchain manual debug run, use `LSAN_OPTIONS=suppressions=$PWD/tests/lsan.supp` if
this external cache report occurs. Clang tests locally suppress doctest 2.4.12's
retired `<ciso646>` header warning only around that upstream include.

## Windows setup

Install Visual Studio's C++ x64 desktop tools, CMake 3.28+, Ninja, and the
[Vulkan SDK](https://vulkan.lunarg.com/sdk/home). In an x64 Native Tools command prompt:

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 4
ctest --preset windows-debug
build\windows-debug\game\microjump.exe --smoke-test
```

Use `windows-release` for a release build. CI builds both MSVC configurations and
uploads `microjump-Windows-Release`, containing a runtime zip with `microjump.exe`,
`SDL3.dll`, the redistributable MSVC runtime DLLs and license notices. Extract the zip,
then run `microjump.exe`; a Vulkan 1.3
GPU driver is required. Release builds do not require the SDK/validation layers.
Shaders and assets will join the package when implemented in later milestones.

## Checks and packaging

```sh
# Add the real display/GPU startup test to CTest (also runs under CI's software Vulkan).
cmake --preset linux-debug -DMJ_SMOKE_TESTS=ON
ctest --preset linux-debug
# Manual equivalent: opens a window, processes a short event sequence, exits.
./build/linux-debug/game/microjump --smoke-test
# Check project formatting and analyze the implementation.
find engine game tests -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror
find engine/src game -name '*.cpp' -print0 | xargs -0 -n 1 clang-tidy -p build/linux-clang-debug
# Package an already-built release.
cpack --config build/linux-release/CPackConfig.cmake -B build/packages
```

Normal startup stays open until Escape or the window close button. `--help` works
without opening a display. Unknown arguments fail explicitly. Frame rendering,
`--frames`, frame capture, the FPS overlay and resize of GPU render targets belong to M1.

CI verifies GCC, Clang and MSVC builds, unit tests, formatting, static analysis, Linux
Vulkan startup and release packaging. Windows GPU startup requires the owner's PC;
unit tests on a hosted Windows runner do not prove that startup works on hardware.
See [docs/m0-evidence.md](docs/m0-evidence.md) for observations and remaining gates.
