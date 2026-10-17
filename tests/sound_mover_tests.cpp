#include "test_framework.hpp"

#include <level_io.hpp>
#include <sound.hpp>

#include <algorithm>
#include <vector>

TEST_CASE("a takeoff from a descending lift sounds once even with downward world velocity") {
    const auto level = mj::game::parse_level(R"({
      "version": 1, "name": "Descending lift sound", "kill_height": -60,
      "shapes": [{"kind":"box", "center":[0,-40.5,0], "size":[100,1,100], "color":"#808080"}],
      "checkpoints": [{"position":[0,0.01,0]}],
      "movers": [{"name":"Lift", "pivot":[0,0,0], "path":[[0,0,0],[0,20,0]],
        "period":5, "sound":"none", "shapes":[
          {"kind":"box", "center":[0,-0.25,0], "size":[4,0.5,4], "color":"#b08d57"}]}]
    })");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);
    mj::game::Simulation simulation(*level, geometry.collision);
    mj::game::Simulation silent(*level, geometry.collision);
    mj::game::SoundDirector director;
    director.reset(simulation);
    std::vector<mj::game::CueEvent> heard;
    constexpr float dt = 1.0F / 120;
    const auto tick = [&](mj::TickInput input) {
        const auto before = simulation.player.current();
        simulation.tick(input, dt);
        director.tick(before, simulation, dt, heard);
        silent.tick(input, dt);
        const auto &a = simulation.player.current();
        const auto &b = silent.player.current();
        CHECK(a.position.x == b.position.x);
        CHECK(a.position.y == b.position.y);
        CHECK(a.position.z == b.position.z);
        CHECK(a.velocity.x == b.velocity.x);
        CHECK(a.velocity.y == b.velocity.y);
        CHECK(a.velocity.z == b.velocity.z);
        CHECK(a.state == b.state);
        CHECK(a.grounded == b.grounded);
    };
    // At 3.75 seconds the return leg is halfway done. Its eased 20 m leg takes 2.5 s,
    // giving peak speed pi * 20 / (2 * 2.5), about 12.6 m/s: below the parser's 20 m/s
    // cap but faster than a standing jump's upward launch, so leaving still travels down.
    for (int i = 0; i < 450; ++i)
        tick({});
    REQUIRE(simulation.player.current().grounded);
    REQUIRE(simulation.player.current().ground_body == 0U);
    heard.clear();
    tick({0, 0, 0, 0, mj::input_button::jump_pressed | mj::input_button::jump_held});
    REQUIRE_FALSE(simulation.player.current().grounded);
    REQUIRE(simulation.player.current().state == mj::game::MoveState::jump);
    REQUIRE(simulation.player.current().velocity.y < 0.0F);
    const auto jumps = [&] {
        return std::count_if(heard.begin(), heard.end(),
                             [](const auto &event) { return event.cue == mj::game::Cue::jump; });
    };
    CHECK(jumps() == 1);
    for (int i = 0; i < 120; ++i)
        tick({0, 0, 0, 0, mj::input_button::jump_held});
    CHECK(jumps() == 1);
}
