#pragma once

#include <mj/core/error.hpp>

#include <expected>
#include <span>
#include <string_view>

namespace mj {

struct LaunchOptions {
    bool help = false;
    bool smoke_test = false;
};

[[nodiscard]] std::expected<LaunchOptions, Error>
parse_launch_options(std::span<const std::string_view> arguments);

} // namespace mj
