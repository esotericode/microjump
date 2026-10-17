#include "test_framework.hpp"

#include <level_io.hpp>

#include <mj/core/launch_options.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
mj::LaunchOptions parse(std::vector<std::string_view> arguments) {
    auto options = mj::parse_launch_options(arguments);
    REQUIRE(options);
    return *options;
}
} // namespace

TEST_CASE("the pause menu's places follow the gates out from the starting level") {
    const auto places =
        mj::game::discover_places(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(places.size() == 3);
    CHECK(places[0].name == "microjump-playground");
    CHECK(places[1].title == "The Lantern Isles");
    CHECK(places[2].title == "The Clockwork Garden");
    for (const auto &place : places) {
        CAPTURE(place.name);
        CHECK(place.sparks == 16);
        CHECK(std::filesystem::exists(place.path));
    }
    // From the garden, the playground comes second, then the isles through it.
    const auto from_garden =
        mj::game::discover_places(std::filesystem::path(MJ_TEST_LEVELS) / "clockwork-garden.json");
    REQUIRE(from_garden.size() == 3);
    CHECK(from_garden[1].name == "microjump-playground");
}

TEST_CASE("a gate to a missing level is left out of the places") {
    const auto folder = std::filesystem::path(MJ_TEST_OUTPUT) / "places";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "start.json") << R"({"version":1, "name":"Start",
      "shapes":[{"kind":"box","center":[0,-0.5,0],"size":[10,1,10],"color":"#808080"}],
      "checkpoints":[{"position":[0,0.01,0]}],
      "portals":[{"name":"Nowhere","position":[0,1.5,3],"level":"missing.json"},
                 {"name":"Home","position":[0,1.5,-3],"level":"start.json"}]})";
    const auto places = mj::game::discover_places(folder / "start.json");
    REQUIRE(places.size() == 1); // And a gate back to itself isn't a second place.
    CHECK(places[0].title == "Start");
    CHECK(mj::game::discover_places(folder / "missing.json").empty());
}

TEST_CASE("only a person at the game can pause it; captures can show a menu page") {
    CHECK(mj::is_interactive(parse({})));
    CHECK(mj::is_interactive(parse({"--replay", "a.mjr"})));
    CHECK(mj::is_interactive(parse({"--record", "a.mjr"})));
    CHECK_FALSE(mj::is_interactive(parse({"--capture-frame", "3", "--capture-out", "a.png"})));
    CHECK_FALSE(mj::is_interactive(parse({"--travel-test"})));
    CHECK(parse({"--show-menu", "places"}).show_menu == mj::MenuPage::places);
    CHECK(parse({"--show-menu", "quit"}).show_menu == mj::MenuPage::quit);
    CHECK_FALSE(mj::parse_launch_options(std::vector<std::string_view>{"--show-menu", "maps"}));
    CHECK_FALSE(mj::parse_launch_options(std::vector<std::string_view>{"--show-menu"}));
    CHECK_FALSE(mj::parse_launch_options(
        std::vector<std::string_view>{"--show-menu", "main", "--show-menu", "main"}));
}
