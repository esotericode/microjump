#pragma once

#include "level.hpp"

#include <mj/core/error.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mj::game {

// Version 1: meters, radians, sRGB hex albedo and linear light colors. See docs/levels.md.
[[nodiscard]] std::expected<Level, Error> parse_level(std::string_view text);
[[nodiscard]] std::expected<Level, Error> load_level(const std::filesystem::path &path);
// A level the game knows about: the starting one and every level its gates lead to.
struct Place {
    std::filesystem::path path;
    std::string name;  // The level's identity, as saves record it.
    std::string title; // What players see.
    std::size_t sparks = 0;
};

// Follows gates outward from the starting level, in the order they're first reached. A gate
// to a missing or broken level is skipped (the gate itself says why when you walk in).
[[nodiscard]] std::vector<Place> discover_places(const std::filesystem::path &start);

// A plain JSON file beside a loaded level; shared by travel and saved progress.
[[nodiscard]] bool valid_level_file(std::string_view text);

} // namespace mj::game
