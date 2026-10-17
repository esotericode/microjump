#include "settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace mj::game {
namespace {
using Json = nlohmann::json;
constexpr std::size_t k_max_settings_bytes = 16'384;

std::string display_path(const std::filesystem::path &path) {
    std::string text;
    try {
        for (const char8_t byte : path.u8string())
            text.push_back(static_cast<char>(byte));
    } catch (const std::filesystem::filesystem_error &) {
        return "<unrepresentable path>";
    }
    return text;
}

bool bounded_depth(std::string_view text) {
    int depth = 0;
    bool quoted = false, escaped = false;
    for (const char c : text) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > 8)
                return false;
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

bool fields(const Json &value, std::initializer_list<std::string_view> keys) {
    if (!value.is_object() || value.size() != keys.size())
        return false;
    return std::all_of(keys.begin(), keys.end(), [&](auto key) { return value.contains(key); });
}

bool integer(const Json &value, int low, int high) {
    return value.is_number_integer() && value >= low && value <= high;
}

bool number(const Json &value, double low, double high) {
    if (!value.is_number())
        return false;
    const double scalar = value.get<double>();
    return std::isfinite(scalar) && scalar >= low && scalar <= high;
}

std::expected<std::string, Error> encode_settings(const UserSettings &settings) {
    const auto &display = settings.display;
    const auto &player = settings.player;
    const Json root = {{"version", 1},
                       {"display",
                        {{"width", display.width},
                         {"height", display.height},
                         {"fullscreen", display.fullscreen},
                         {"vsync", display.vsync},
                         {"frame_limit", display.frame_limit}}},
                       {"player",
                        {{"mouse_sensitivity", player.mouse_sensitivity},
                         {"stick_speed", player.stick_speed},
                         {"invert_y", player.invert_y}}},
                       {"audio", {{"volume", settings.volume}, {"muted", settings.muted}}}};
    auto text = root.dump(2) + '\n';
    if (auto valid = parse_settings(text); !valid)
        return std::unexpected(valid.error());
    return text;
}

// A unique directory puts the staged file on the destination filesystem and avoids
// concurrent writers sharing a temporary filename.
std::expected<std::filesystem::path, Error> staging_directory(const std::filesystem::path &parent) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto candidate =
            parent / (".microjump-settings-" + std::to_string(time) + "-" +
                      std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error))
            return candidate;
        if (error)
            return std::unexpected(Error{"Cannot stage settings: " + error.message()});
    }
    return std::unexpected(Error{"Cannot allocate a unique settings staging directory"});
}

class StagedSettings {
  public:
    explicit StagedSettings(std::filesystem::path directory)
        : m_directory(std::move(directory)), file(m_directory / "settings.tmp") {}
    ~StagedSettings() {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(m_directory, ignored);
    }
    StagedSettings(const StagedSettings &) = delete;
    StagedSettings &operator=(const StagedSettings &) = delete;

  private:
    std::filesystem::path m_directory;

  public:
    std::filesystem::path file;
};
} // namespace

std::expected<UserSettings, Error> parse_settings(std::string_view text) {
    if (text.size() > k_max_settings_bytes || !bounded_depth(text))
        return std::unexpected(Error{"Settings exceed the 16 KiB / 8-level nesting limit"});
    bool duplicate_key = false;
    std::vector<std::set<std::string>> keys;
    const auto check_keys = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.empty())
            duplicate_key = !keys.back().insert(value.get<std::string>()).second || duplicate_key;
        else if (event == Json::parse_event_t::object_end && !keys.empty())
            keys.pop_back();
        return true;
    };
    const auto root = Json::parse(text, check_keys, false);
    if (root.is_discarded())
        return std::unexpected(Error{"Settings are not valid JSON"});
    if (duplicate_key)
        return std::unexpected(Error{"Settings contain a duplicate object field"});
    if (!fields(root, {"version", "display", "player", "audio"}) || !integer(root["version"], 1, 1))
        return std::unexpected(
            Error{"Settings require format version 1, display, player and audio"});
    const auto &display = root["display"];
    if (!fields(display, {"width", "height", "fullscreen", "vsync", "frame_limit"}) ||
        !integer(display["width"], 640, 7680) || !integer(display["height"], 360, 4320) ||
        !display["fullscreen"].is_boolean() || !display["vsync"].is_boolean() ||
        !integer(display["frame_limit"], 0, 120))
        return std::unexpected(Error{"Settings display requires a 640..7680 by 360..4320 size, "
                                     "fullscreen, vsync and a frame limit"});
    const int frame_limit = display["frame_limit"].get<int>();
    if (frame_limit != 0 && frame_limit != 30 && frame_limit != 60 && frame_limit != 120)
        return std::unexpected(Error{"Settings frame limit must be 0, 30, 60 or 120"});
    const auto &player = root["player"];
    if (!fields(player, {"mouse_sensitivity", "stick_speed", "invert_y"}) ||
        !number(player["mouse_sensitivity"], 0.25, 3) || !number(player["stick_speed"], 0.25, 3) ||
        !player["invert_y"].is_boolean())
        return std::unexpected(
            Error{"Settings player requires look speeds in [0.25, 3] and invert_y"});
    const auto &audio = root["audio"];
    if (!fields(audio, {"volume", "muted"}) || !number(audio["volume"], 0, 1) ||
        !audio["muted"].is_boolean())
        return std::unexpected(Error{"Settings audio requires volume in [0, 1] and muted"});
    return UserSettings{{display["width"].get<int>(), display["height"].get<int>(),
                         display["fullscreen"].get<bool>(), display["vsync"].get<bool>(),
                         frame_limit},
                        {player["mouse_sensitivity"].get<float>(),
                         player["stick_speed"].get<float>(), player["invert_y"].get<bool>()},
                        audio["volume"].get<float>(),
                        audio["muted"].get<bool>()};
}

std::expected<UserSettings, Error> load_settings(const std::filesystem::path &path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return std::unexpected(Error{"Cannot inspect settings: " + error.message()});
    if (!exists)
        return UserSettings{};
    const auto name = display_path(path);
    if (!std::filesystem::is_regular_file(path, error) || error)
        return std::unexpected(Error{"Settings are not a readable file: " + name});
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected(Error{"Cannot open settings: " + name});
    const auto size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(k_max_settings_bytes))
        return std::unexpected(Error{"Settings are empty or exceed 16 KiB: " + name});
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(text.data(), size);
    if (!file)
        return std::unexpected(Error{"Cannot read settings: " + name});
    auto settings = parse_settings(text);
    if (!settings)
        settings.error().message = name + ": " + settings.error().message;
    return settings;
}

std::expected<void, Error> save_settings(const std::filesystem::path &path,
                                         const UserSettings &settings) {
    const auto text = encode_settings(settings);
    if (!text)
        return std::unexpected(text.error());
    if (path.empty() || path.filename().empty() || path.filename() == "." ||
        path.filename() == "..")
        return std::unexpected(Error{"Settings require a file path"});
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error)
        return std::unexpected(Error{"Cannot create settings directory: " + error.message()});
    auto directory = staging_directory(parent);
    if (!directory)
        return std::unexpected(directory.error());
    const StagedSettings staged(std::move(*directory));
    std::ofstream file(staged.file, std::ios::binary | std::ios::trunc);
    file.write(text->data(), static_cast<std::streamsize>(text->size()));
    file.close();
    if (!file)
        return std::unexpected(Error{"Cannot write staged settings: " + display_path(path)});
#ifdef _WIN32
    if (MoveFileExW(staged.file.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(staged.file, path, error);
#endif
    if (error)
        return std::unexpected(Error{"Cannot replace settings: " + error.message()});
    return {};
}

bool uses_saved_settings(const LaunchOptions &options) {
    return is_interactive(options) && uses_saved_progress(options) && options.save.empty() &&
           !options.show_menu;
}

} // namespace mj::game
