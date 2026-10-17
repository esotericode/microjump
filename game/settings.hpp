#pragma once

#include <mj/core/error.hpp>
#include <mj/core/launch_options.hpp>

#include <expected>
#include <filesystem>
#include <string_view>

namespace mj::game {

struct DisplayOptions {
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    int frame_limit = 0; // Unlimited, or 30, 60 or 120 frames per second.
    bool operator==(const DisplayOptions &) const = default;
};

struct PlayerOptions {
    float mouse_sensitivity = 1;
    float stick_speed = 1;
    bool invert_y = false;
    bool operator==(const PlayerOptions &) const = default;
};

struct UserSettings {
    DisplayOptions display;
    PlayerOptions player;
    float volume = 0.8F;
    bool muted = false;
    bool operator==(const UserSettings &) const = default;
};

[[nodiscard]] std::expected<UserSettings, Error> parse_settings(std::string_view text);
// A missing file starts with defaults. Invalid existing settings stay an error, so the
// caller can keep playing with defaults without overwriting the original file.
[[nodiscard]] std::expected<UserSettings, Error> load_settings(const std::filesystem::path &path);
[[nodiscard]] std::expected<void, Error> save_settings(const std::filesystem::path &path,
                                                       const UserSettings &settings);
// Ordinary live play owns these preferences. Captures, recordings and isolated profiles
// must not read or overwrite the player's settings.
[[nodiscard]] bool uses_saved_settings(const LaunchOptions &options);

} // namespace mj::game
