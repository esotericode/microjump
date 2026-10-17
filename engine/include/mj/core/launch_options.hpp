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

enum class ProgressTest : std::uint8_t { collect, inspect, reset };

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
    std::filesystem::path save; // Explicit saved-expedition profile, including custom levels.
    bool no_save = false;
    bool no_audio = false;
    std::optional<ProgressTest> progress_test; // Isolated process-level progress checks.
};

[[nodiscard]] std::expected<LaunchOptions, Error>
parse_launch_options(std::span<const std::string_view> arguments);
// Only normal live play or an explicit progress fixture may read or write expedition saves.
[[nodiscard]] bool uses_saved_progress(const LaunchOptions &options);
// Sound is for play (live or watching a replay); scripted checks and captures stay silent.
[[nodiscard]] bool uses_audio(const LaunchOptions &options);

} // namespace mj
