#include "test_framework.hpp"

#include <settings.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr std::string_view k_settings = R"({
  "version":1,
  "display":{"width":1920,"height":1080,"fullscreen":true,"vsync":false,"frame_limit":120},
  "player":{"mouse_sensitivity":1.5,"stick_speed":0.75,"invert_y":true},
  "audio":{"volume":0.6,"muted":true}
})";

std::string changed(std::string_view from, std::string_view to) {
    std::string text(k_settings);
    const auto offset = text.find(from);
    REQUIRE(offset != std::string::npos);
    text.replace(offset, from.size(), to);
    return text;
}

class TemporarySettingsDirectory {
  public:
    TemporarySettingsDirectory() {
        static std::atomic<unsigned int> sequence{0};
        const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 32; ++i) {
            path = std::filesystem::current_path() /
                   ("settings-tests-" + std::to_string(time) + "-" +
                    std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            if (std::filesystem::create_directory(path))
                return;
        }
        REQUIRE(false);
    }
    ~TemporarySettingsDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TemporarySettingsDirectory(const TemporarySettingsDirectory &) = delete;
    TemporarySettingsDirectory &operator=(const TemporarySettingsDirectory &) = delete;
    std::filesystem::path path;
};

std::string read_text(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    REQUIRE(file.good());
}
} // namespace

TEST_CASE("player settings load display, camera and audio preferences together") {
    const auto settings = mj::game::parse_settings(k_settings);
    REQUIRE(settings);
    CHECK(settings->display == mj::game::DisplayOptions{1920, 1080, true, false, 120});
    CHECK(settings->player == mj::game::PlayerOptions{1.5F, 0.75F, true});
    CHECK(settings->volume == 0.6F);
    CHECK(settings->muted);
    for (const int frame_limit : {0, 30, 60, 120}) {
        const auto capped = mj::game::parse_settings(
            changed("\"frame_limit\":120", "\"frame_limit\":" + std::to_string(frame_limit)));
        REQUIRE(capped);
        CHECK(capped->display.frame_limit == frame_limit);
    }
    CHECK(mj::game::parse_settings(changed("1920", "640")));
    CHECK(mj::game::parse_settings(changed("1920", "7680")));
    CHECK(mj::game::parse_settings(changed("1080", "360")));
    CHECK(mj::game::parse_settings(changed("1080", "4320")));
    CHECK(mj::game::parse_settings(changed("1.5", "0.25")));
    CHECK(mj::game::parse_settings(changed("1.5", "3")));
    CHECK(mj::game::parse_settings(changed("0.6", "0")));
    CHECK(mj::game::parse_settings(changed("0.6", "1")));
}

TEST_CASE("player settings reject malformed data and unsupported or unsafe values") {
    CHECK_FALSE(mj::game::parse_settings(""));
    CHECK_FALSE(mj::game::parse_settings("{"));
    CHECK_FALSE(mj::game::parse_settings("[]"));
    for (const auto &[from, to] : {std::pair{"\"version\":1", "\"version\":2"},
                                   {"\"version\":1", "\"version\":1.0"},
                                   {"\"version\":1", "\"version\":true"},
                                   {"\"version\":1,", ""},
                                   {"\"version\":1", "\"version\":1,\"version\":1"},
                                   {"\"version\":1", "\"version\":1,\"unknown\":true"},
                                   {"\"width\":1920", "\"width\":1920,\"width\":1920"},
                                   {"\"width\":1920", "\"width\":1920,\"unknown\":true"},
                                   {"1920", "639"},
                                   {"1920", "7681"},
                                   {"1920", "1920.5"},
                                   {"1920", "18446744073709551615"},
                                   {"1080", "359"},
                                   {"1080", "4321"},
                                   {"\"fullscreen\":true", "\"fullscreen\":1"},
                                   {"\"vsync\":false", "\"vsync\":null"},
                                   {"\"frame_limit\":120", "\"frame_limit\":144"},
                                   {"\"frame_limit\":120", "\"frame_limit\":45"},
                                   {"\"frame_limit\":120", "\"frame_limit\":-1"},
                                   {"\"frame_limit\":120", "\"frame_limit\":60.0"},
                                   {"\"frame_limit\":120", "\"frame_limit\":true"},
                                   {"1.5", "0.249"},
                                   {"1.5", "3.001"},
                                   {"1.5", "1e999"},
                                   {"1.5", "null"},
                                   {"0.75", "false"},
                                   {"\"invert_y\":true", "\"invert_y\":\"true\""},
                                   {"0.6", "-0.01"},
                                   {"0.6", "1.01"},
                                   {"0.6", "true"},
                                   {"\"muted\":true", "\"muted\":0"}}) {
        CAPTURE(from);
        CAPTURE(to);
        CHECK_FALSE(mj::game::parse_settings(changed(from, to)));
    }
    CHECK_FALSE(mj::game::parse_settings(std::string(16 * 1024 + 1, ' ')));
    CHECK_FALSE(mj::game::parse_settings(std::string(9, '[') + std::string(9, ']')));
}

TEST_CASE("settings save atomically on Unicode paths and preserve files after failed writes") {
    TemporarySettingsDirectory directory;
    const auto path =
        directory.path / std::filesystem::path(u8"preferences-\u96ea") / "settings.json";
    const auto missing = mj::game::load_settings(path);
    REQUIRE(missing);
    CHECK(*missing == mj::game::UserSettings{});
    CHECK_FALSE(std::filesystem::exists(path.parent_path()));
    auto settings = mj::game::parse_settings(k_settings);
    REQUIRE(settings);
    REQUIRE(mj::game::save_settings(path, *settings));
    auto loaded = mj::game::load_settings(path);
    REQUIRE(loaded);
    CHECK(*loaded == *settings);
    settings->display = {2560, 1440, false, true, 60};
    settings->player.mouse_sensitivity = 1.2345678F;
    settings->volume = 0.1234567F;
    REQUIRE(mj::game::save_settings(path, *settings));
    loaded = mj::game::load_settings(path);
    REQUIRE(loaded);
    CHECK(*loaded == *settings);
    const auto old_bytes = read_text(path);
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(), -1.0F, 1.1F}) {
        settings->volume = invalid;
        CHECK_FALSE(mj::game::save_settings(path, *settings));
        CHECK(read_text(path) == old_bytes);
    }
    settings->volume = 0.6F;
    settings->display.frame_limit = 45;
    CHECK_FALSE(mj::game::save_settings(path, *settings));
    CHECK(read_text(path) == old_bytes);
    CHECK(std::distance(std::filesystem::directory_iterator(path.parent_path()),
                        std::filesystem::directory_iterator()) == 1);
    const auto blocker = path.parent_path() / "occupied";
    REQUIRE(std::filesystem::create_directory(blocker));
    write_text(blocker / "keep.txt", "Preserve this file");
    CHECK_FALSE(mj::game::save_settings(blocker, {}));
    CHECK(read_text(blocker / "keep.txt") == "Preserve this file");
    CHECK_FALSE(mj::game::save_settings(path / "child.json", {}));
    CHECK(read_text(path) == old_bytes);
    CHECK_FALSE(mj::game::save_settings({}, {}));
}

TEST_CASE("invalid existing settings remain errors instead of silently becoming defaults") {
    TemporarySettingsDirectory directory;
    const auto path = directory.path / "settings.json";
    CHECK_FALSE(mj::game::load_settings(directory.path));
    for (const auto &text :
         {std::string{}, std::string{"broken json"}, std::string(16 * 1024 + 1, ' ')}) {
        write_text(path, text);
        CHECK_FALSE(mj::game::load_settings(path));
        CHECK(read_text(path) == text);
    }
}

TEST_CASE("scripted runs and isolated profiles never use personal settings") {
    const auto parse = [](std::vector<std::string_view> arguments) {
        auto options = mj::parse_launch_options(arguments);
        REQUIRE(options);
        return *options;
    };
    CHECK(mj::game::uses_saved_settings(parse({})));
    CHECK(mj::game::uses_saved_settings(parse({"--no-audio"})));
    for (const auto &arguments : std::vector<std::vector<std::string_view>>{
             {"--no-save"},
             {"--save", "isolated.json"},
             {"--record", "a.mjr"},
             {"--replay", "a.mjr"},
             {"--level", "custom.json"},
             {"--frames", "3"},
             {"--smoke-test"},
             {"--resize-test"},
             {"--camera-test"},
             {"--travel-test"},
             {"--pacing-test"},
             {"--show-menu", "options"},
             {"--frame-log", "frames.csv"},
             {"--capture-frame", "3", "--capture-out", "a.png"},
             {"--capture-tick", "3", "--replay", "a.mjr", "--capture-out", "a.png"},
             {"--progress-test", "inspect", "--save", "isolated.json"}}) {
        CHECK_FALSE(mj::game::uses_saved_settings(parse(arguments)));
    }
}
