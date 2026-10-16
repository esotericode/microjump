#pragma once

#include <mj/core/error.hpp>
#include <mj/render/lighting.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace mj {

struct LaunchOptions {
    bool help = false;
    bool smoke_test = false;
    bool resize_test = false;
    bool camera_test = false;
    bool hide_overlay = false;
    bool pacing_test = false; // Scripted shadow/stall/swapchain sequence with timing summaries.
    bool travel_test = false; // Through the level's first gate and back, swapping meshes.
    std::optional<std::uint32_t> frames;
    std::optional<std::uint32_t> capture_frame;
    std::optional<std::uint32_t> capture_tick; // Exact simulation state, without interpolation.
    std::filesystem::path capture_out;
    std::filesystem::path record;
    std::filesystem::path replay;
    std::filesystem::path level; // Empty uses assets/levels/playground.json beside the executable.
    std::filesystem::path frame_log;  // Per-frame timing CSV (frame-pacing diagnostics).
    std::optional<float> time_of_day; // Hours, 0..24.
    std::optional<ShadowQuality> shadow_quality;
};

[[nodiscard]] std::expected<LaunchOptions, Error>
parse_launch_options(std::span<const std::string_view> arguments);

} // namespace mj
