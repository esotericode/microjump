# Third-party notices and pins

CMake downloads only these libraries. Pins are immutable
commits plus verified SHA-256 archive hashes in `cmake/Dependencies.cmake`.

| Library | Version / commit | Purpose | License |
| --- | --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.16 / `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` | Window and platform events | zlib |
| [volk](https://github.com/zeux/volk) | vulkan-sdk-1.4.341.0 / `d979819fd03de3a7606d6e23d6ff4968942599da` | Vulkan function loading | MIT |
| [doctest](https://github.com/doctest/doctest) | 2.4.12 / `1da23a3e8119ec5cce4f9388e91b065e20bf06f5` | Tests only | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91.9b / `f5befd2d29e66809cd1110a152e375a7f1981f06` | Debug overlay, SDL3/Vulkan backends only | MIT |
| [stb](https://github.com/nothings/stb) | `2c980bb59875b0d32144a71867fbdebb2f77cd20` | PNG writer; PNG decoder in tests only | MIT / public domain |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 / `55f93686c01528224f448c19128836e7df245f72` | Versioned level-file parsing | MIT |

Vulkan headers/loader, GPU drivers and development tools come from the platform
SDK/distribution. They are not bundled with the game.

`cmake --install` and CPack include these libraries' upstream license notices in
`licenses/`, plus the project's MIT license at the package root. SDL's shared library
is included; the Vulkan runtime must be installed with the user's GPU driver.
