#include "test_framework.hpp"

#include <mj/core/input_record.hpp>
#include <mj/core/launch_options.hpp>

#include <array>
#include <bit>
#include <filesystem>

namespace button = mj::input_button;

TEST_CASE("replay text round-trips every float bit and compresses repeated ticks") {
    std::vector<mj::TickInput> ticks;
    for (int i = 0; i < 200; ++i)
        ticks.push_back({0, 1, -0.66F, -0.32F, button::jump_held});
    ticks.push_back({0.70710677F, -0.70710677F, 3.1415925F, -1.25F, button::jump_pressed});
    ticks.push_back(
        {-0.0F, 1.0e-7F, -2.0e-38F, 0.1F, button::reset_pressed | button::long_jump_held});
    const auto text = mj::serialize_replay(ticks);
    CHECK(text.rfind("microjump-replay 3\ntick-rate 120\n", 0) == 0);
    CHECK(std::count(text.begin(), text.end(), '\n') == 6); // Header, comment, 3 runs.
    const auto parsed = mj::parse_replay(text);
    REQUIRE(parsed);
    REQUIRE(parsed->size() == ticks.size());
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        CHECK(std::bit_cast<std::uint32_t>((*parsed)[i].move_x) ==
              std::bit_cast<std::uint32_t>(ticks[i].move_x));
        CHECK(std::bit_cast<std::uint32_t>((*parsed)[i].camera_yaw) ==
              std::bit_cast<std::uint32_t>(ticks[i].camera_yaw));
        CHECK((*parsed)[i].buttons == ticks[i].buttons);
    }
    const auto path = std::filesystem::path("replay-roundtrip.mjr");
    REQUIRE(mj::save_replay(path, ticks));
    const auto loaded = mj::load_replay(path);
    REQUIRE(loaded);
    CHECK(loaded->size() == ticks.size());
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST_CASE("version 3 replays name their level; older versions still load without one") {
    const std::vector<mj::TickInput> ticks{{0, 1, 0, 0, button::jump_held}};
    const auto text = mj::serialize_replay(ticks, "Plaza \xC3\xA9t\xC3\xA9"); // UTF-8.
    CHECK(text.rfind("microjump-replay 3\ntick-rate 120\nlevel Plaza \xC3\xA9t\xC3\xA9\n", 0) == 0);
    const auto parsed = mj::parse_replay_file(text);
    REQUIRE(parsed);
    CHECK(parsed->level == "Plaza \xC3\xA9t\xC3\xA9");
    CHECK(parsed->ticks.size() == 1);
    // Unnamed or unsafe names are simply not recorded.
    for (const std::string_view name : {"", "two\nlines", "tab\tname"}) {
        const auto unnamed = mj::parse_replay_file(mj::serialize_replay(ticks, name));
        REQUIRE(unnamed);
        CHECK(unnamed->level.empty());
    }
    const std::string v3 = "microjump-replay 3\ntick-rate 120\n";
    CHECK(mj::parse_replay_file(v3 + "# comment\nlevel yard\n1 0 1 0 0 0\n"));
    CHECK_FALSE(mj::parse_replay_file(v3 + "level yard\nlevel again\n"));
    CHECK_FALSE(mj::parse_replay_file(v3 + "1 0 1 0 0 0\nlevel late\n"));
    CHECK_FALSE(mj::parse_replay_file(v3 + "level \n"));
    CHECK_FALSE(mj::parse_replay_file("microjump-replay 2\ntick-rate 120\nlevel yard\n"));
    for (const char *old : {"microjump-replay 1\ntick-rate 120\n1 0 1 0 0 7\n",
                            "microjump-replay 2\ntick-rate 120\n1 0 1 0 0 11\n"}) {
        const auto legacy = mj::parse_replay_file(old);
        REQUIRE(legacy);
        CHECK(legacy->level.empty());
        CHECK(legacy->ticks.size() == 1);
    }
}

TEST_CASE("replay parser rejects malformed, oversized and unknown input") {
    const std::string header = "microjump-replay 1\ntick-rate 120\n";
    CHECK_FALSE(mj::parse_replay(""));
    CHECK_FALSE(mj::parse_replay("microjump-replay 4\ntick-rate 120\n"));
    CHECK_FALSE(mj::parse_replay("microjump-replay 1\ntick-rate 60\n"));
    const auto empty = mj::parse_replay(header);
    REQUIRE(empty);
    CHECK(empty->empty());
    CHECK_FALSE(mj::parse_replay(header + "1 0 1 0 0\n"));             // Missing field.
    CHECK_FALSE(mj::parse_replay(header + "1 0 1 0 0 0 7\n"));         // Extra field.
    CHECK_FALSE(mj::parse_replay(header + "0 0 1 0 0 0\n"));           // Empty run.
    CHECK_FALSE(mj::parse_replay(header + "1 0 1.5 0 0 0\n"));         // Stick too long.
    CHECK_FALSE(mj::parse_replay(header + "1 0 nan 0 0 0\n"));         // Non-finite.
    CHECK_FALSE(mj::parse_replay(header + "1 0 1 0 0 64\n"));          // Unknown button.
    CHECK_FALSE(mj::parse_replay(header + "1 0 1x 0 0 0\n"));          // Trailing junk.
    CHECK_FALSE(mj::parse_replay(header + "99999999999 0 1 0 0 0\n")); // Too long.
    // A maximum-sized run after a valid tick must not wrap the total and reach vector::insert.
    CHECK_FALSE(mj::parse_replay(header + "1 0 0 0 0 0\n18446744073709551615 0 0 0 0 0\n"));
    const auto ok = mj::parse_replay(header + "# comment\r\n\n3 0 1 0 0 1\r\n");
    REQUIRE(ok);
    CHECK(ok->size() == 3);
    CHECK_FALSE(mj::load_replay("does-not-exist.mjr"));
}

TEST_CASE("record and replay command line options are validated") {
    auto parse = [](std::initializer_list<std::string_view> args) {
        return mj::parse_launch_options({args.begin(), args.size()});
    };
    CHECK(parse({"--record", "a.mjr"})->record == "a.mjr");
    CHECK(parse({"--replay", "a.mjr"})->replay == "a.mjr");
    CHECK_FALSE(parse({"--record"}));
    CHECK_FALSE(parse({"--record", "a.mjr", "--replay", "b.mjr"}));
    CHECK_FALSE(parse({"--replay", "a.mjr", "--replay", "b.mjr"}));
    CHECK_FALSE(parse({"--camera-test", "--replay", "a.mjr"}));
    CHECK(parse({"--level", "yard.json"})->level == "yard.json");
    CHECK_FALSE(parse({"--level"}));
    CHECK_FALSE(parse({"--level", "a.json", "--level", "b.json"}));
}
