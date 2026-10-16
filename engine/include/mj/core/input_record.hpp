#pragma once

#include <mj/core/error.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mj {

namespace input_button {
inline constexpr std::uint32_t jump_held = 1U << 0U;
inline constexpr std::uint32_t jump_pressed = 1U << 1U; // Press edge latched until a tick.
inline constexpr std::uint32_t reset_pressed = 1U << 2U;
inline constexpr std::uint32_t long_jump_held = 1U << 3U; // Version 2: momentum modifier.
inline constexpr std::uint32_t v1 = jump_held | jump_pressed | reset_pressed;
inline constexpr std::uint32_t all = v1 | long_jump_held;
} // namespace input_button

// Everything the simulation reads for one fixed tick. Replaying the same sequence
// reproduces the same simulation regardless of render rate, FPS cap or VSync.
struct TickInput {
    float move_x = 0; // Camera-relative stick: +X right, +Y forward, length <= 1.
    float move_y = 0;
    float camera_yaw = 0;
    float camera_pitch = 0;
    std::uint32_t buttons = 0;

    [[nodiscard]] bool held(std::uint32_t button) const {
        return (buttons & button) != 0;
    }
};

// Versioned, line-based text format with run-length encoding and exact float round trips.
[[nodiscard]] std::string serialize_replay(std::span<const TickInput> ticks);
[[nodiscard]] std::expected<std::vector<TickInput>, Error> parse_replay(std::string_view text);
[[nodiscard]] std::expected<void, Error> save_replay(const std::filesystem::path &path,
                                                     std::span<const TickInput> ticks);
[[nodiscard]] std::expected<std::vector<TickInput>, Error>
load_replay(const std::filesystem::path &path);

} // namespace mj
