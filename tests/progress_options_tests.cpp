#include "test_framework.hpp"

#include <mj/core/launch_options.hpp>

#include <initializer_list>
#include <string_view>
#include <vector>

namespace {
std::expected<mj::LaunchOptions, mj::Error>
parse(std::initializer_list<std::string_view> arguments) {
    return mj::parse_launch_options({arguments.begin(), arguments.size()});
}

// Every launch mode here is deterministic, automated or diagnostic, and must stay
// independent of the player's expedition even if a save path was supplied explicitly.
const std::vector<std::vector<std::string_view>> k_isolated_runs{
    {"--help"},
    {"--record", "recording.mjr"},
    {"--replay", "recording.mjr"},
    {"--frames", "30"},
    {"--capture-frame", "12", "--capture-out", "capture.png"},
    {"--capture-tick", "120", "--capture-out", "capture.png"},
    {"--smoke-test"},
    {"--resize-test"},
    {"--camera-test"},
    {"--travel-test"},
    {"--pacing-test"},
    {"--frame-log", "frames.csv"},
};
} // namespace

TEST_CASE("saved-expedition options accept explicit UTF-8 profiles and an opt-out") {
    const auto profile = parse({"--save", "profile-\xC3\xA9.json"});
    REQUIRE(profile);
    CHECK(profile->save.u8string() == u8"profile-\u00e9.json");
    CHECK(mj::uses_saved_progress(*profile));

    const auto disabled = parse({"--save", "profile.json", "--no-save"});
    REQUIRE(disabled);
    CHECK(disabled->no_save);
    CHECK(disabled->save == "profile.json");
    CHECK_FALSE(mj::uses_saved_progress(*disabled));
    const auto reverse = parse({"--no-save", "--save", "profile.json"});
    REQUIRE(reverse);
    CHECK_FALSE(mj::uses_saved_progress(*reverse));
    CHECK_FALSE(parse({"--save"}));
    CHECK_FALSE(parse({"--save", ""}));
    CHECK_FALSE(parse({"--save", "--no-save"}));
    CHECK_FALSE(parse({"--save", "one.json", "--save", "two.json"}));
    CHECK_FALSE(parse({"--no-save", "--no-save"}));
}

TEST_CASE("only normal live play uses the default expedition profile") {
    const auto live = parse({});
    REQUIRE(live);
    CHECK(mj::uses_saved_progress(*live));
    const auto visual = parse({"--hide-overlay", "--time-of-day", "17", "--shadow-quality", "low"});
    REQUIRE(visual);
    CHECK(mj::uses_saved_progress(*visual));
    const auto custom = parse({"--level", "custom.json"});
    REQUIRE(custom);
    CHECK_FALSE(mj::uses_saved_progress(*custom));
    const auto custom_profile = parse({"--level", "custom.json", "--save", "custom-save.json"});
    REQUIRE(custom_profile);
    CHECK(mj::uses_saved_progress(*custom_profile));
    const auto disabled = parse({"--no-save"});
    REQUIRE(disabled);
    CHECK_FALSE(mj::uses_saved_progress(*disabled));

    for (const auto &arguments : k_isolated_runs) {
        CAPTURE(arguments.front());
        const auto isolated = mj::parse_launch_options(arguments);
        REQUIRE(isolated);
        CHECK_FALSE(mj::uses_saved_progress(*isolated));
        auto explicit_arguments = arguments;
        explicit_arguments.insert(explicit_arguments.end(), {"--save", "isolated.json"});
        const auto explicit_profile = mj::parse_launch_options(explicit_arguments);
        REQUIRE(explicit_profile);
        CHECK_FALSE(mj::uses_saved_progress(*explicit_profile));
    }
}

TEST_CASE("progress fixtures require their own profile and valid mode") {
    for (const auto mode : {"inspect", "reset"}) {
        CAPTURE(mode);
        const auto options = parse({"--progress-test", mode, "--save", "isolated.json"});
        REQUIRE(options);
        CHECK(options->progress_test == (std::string_view(mode) == "inspect"
                                             ? mj::ProgressTest::inspect
                                             : mj::ProgressTest::reset));
        CHECK(mj::uses_saved_progress(*options));
    }
    const auto collect =
        parse({"--progress-test", "collect", "--save", "isolated.json", "--level", "fixture.json"});
    REQUIRE(collect);
    CHECK(collect->progress_test == mj::ProgressTest::collect);
    CHECK(mj::uses_saved_progress(*collect));
    CHECK_FALSE(parse({"--progress-test"}));
    CHECK_FALSE(parse({"--progress-test", ""}));
    CHECK_FALSE(parse({"--progress-test", "unknown", "--save", "isolated.json"}));
    CHECK_FALSE(parse({"--progress-test", "inspect"}));
    CHECK_FALSE(parse({"--progress-test", "collect", "--save", "isolated.json"}));
    CHECK_FALSE(parse(
        {"--progress-test", "inspect", "--save", "isolated.json", "--progress-test", "reset"}));
    CHECK_FALSE(parse({"--progress-test", "inspect", "--save", "isolated.json", "--no-save"}));

    for (const auto &conflict : k_isolated_runs) {
        CAPTURE(conflict.front());
        std::vector<std::string_view> arguments{"--progress-test", "inspect", "--save",
                                                "isolated.json"};
        arguments.insert(arguments.end(), conflict.begin(), conflict.end());
        CHECK_FALSE(mj::parse_launch_options(arguments));
    }
}

TEST_CASE("saved-progress policy also isolates partially constructed capture options") {
    // The parser rejects a missing capture half, but callers of the policy should still
    // never open a profile when either half is present.
    mj::LaunchOptions options;
    options.capture_out = "capture.png";
    CHECK_FALSE(mj::uses_saved_progress(options));
    options.capture_out.clear();
    options.capture_frame = 12;
    CHECK_FALSE(mj::uses_saved_progress(options));
    options.capture_frame.reset();
    options.capture_tick = 120;
    CHECK_FALSE(mj::uses_saved_progress(options));
}
