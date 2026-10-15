#pragma once

#include <mj/core/error.hpp>

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
    std::optional<std::uint32_t> frames;
    std::optional<std::uint32_t> capture_frame;
    std::filesystem::path capture_out;
};

[[nodiscard]] std::expected<LaunchOptions, Error>
parse_launch_options(std::span<const std::string_view> arguments);

} // namespace mj
