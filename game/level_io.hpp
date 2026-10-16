#pragma once

#include "level.hpp"

#include <mj/core/error.hpp>

#include <expected>
#include <filesystem>
#include <string_view>

namespace mj::game {

// Version 1: meters, radians, sRGB hex albedo and linear light colors. See docs/levels.md.
[[nodiscard]] std::expected<Level, Error> parse_level(std::string_view text);
[[nodiscard]] std::expected<Level, Error> load_level(const std::filesystem::path &path);

} // namespace mj::game
