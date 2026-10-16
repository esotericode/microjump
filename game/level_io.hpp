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
// A plain JSON file beside a loaded level; shared by travel and saved progress.
[[nodiscard]] bool valid_level_file(std::string_view text);

} // namespace mj::game
