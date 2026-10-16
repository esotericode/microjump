#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <mj/core/fixed_step.hpp>

#include <array>
#include <bit>
#include <string>
#include <string_view>

namespace {
constexpr float k_dt = 1.0F / 120;
using mj::game::Collectible;
using mj::game::Level;
using mj::game::Simulation;

std::string level_json(std::string_view sparks = {}) {
    std::string text = R"({"version":1,"name":"Spark test","kill_height":-25,
        "shapes":[{"kind":"box","center":[0,-0.5,0],"size":[200,1,200],"color":"#808080"}],
        "checkpoints":[{"position":[0,0.005,0]}])";
    if (!sparks.empty())
        text += ",\"collectibles\":" + std::string(sparks);
    return text + "}";
}

Level airborne_level(std::vector<Collectible> sparks) {
    Level level;
    level.checkpoints.push_back({{0, 10, 0}, 0});
    level.collectibles = std::move(sparks);
    return level;
}

void no_gravity(Simulation &simulation) {
    simulation.player.tuning.jump_height = 0;
}

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
} // namespace

TEST_CASE("collectible schema is optional, bounded and preserves named placements") {
    const auto legacy = mj::game::parse_level(level_json());
    REQUIRE(legacy);
    CHECK(legacy->collectibles.empty());
    REQUIRE(mj::game::parse_level(level_json("[]")));
    const auto level = mj::game::parse_level(level_json(R"([
        {"name":"First light","position":[1,2,3]},
        {"name":"Small","position":[-4096,-24.9,4096],"radius":0.1},
        {"name":"Large","position":[0,4096,0],"radius":2}
    ])"));
    REQUIRE(level);
    REQUIRE(level->collectibles.size() == 3);
    CHECK(level->collectibles[0].name == "First light");
    CHECK(level->collectibles[0].position.y == 2.0F);
    CHECK(level->collectibles[0].radius == doctest::Approx(0.65F));
    CHECK(level->collectibles[1].radius == doctest::Approx(0.1F));
    CHECK(level->collectibles[2].radius == 2.0F);

    std::string many = "[";
    for (int i = 0; i < 129; ++i) {
        if (i != 0)
            many += ',';
        many += "{\"name\":\"Spark " + std::to_string(i) + "\",\"position\":[0,1,0]}";
        if (i == 127) {
            const auto maximum = mj::game::parse_level(level_json(many + "]"));
            REQUIRE(maximum);
            CHECK(maximum->collectibles.size() == 128);
        }
    }
    CHECK_FALSE(mj::game::parse_level(level_json(many + "]")));
}

TEST_CASE("collectible schema rejects malformed and ambiguous spark data with a path") {
    for (const std::string_view sparks :
         {"null",
          "{}",
          "[null]",
          "[[]]",
          "[{}]",
          R"([{"position":[0,1,0]}])",
          R"([{"name":"Spark"}])",
          R"([{"name":"","position":[0,1,0]}])",
          R"([{"name":4,"position":[0,1,0]}])",
          R"([{"name":"Two\nlines","position":[0,1,0]}])",
          R"([{"name":"Spark","position":null}])",
          R"([{"name":"Spark","position":[0,1]}])",
          R"([{"name":"Spark","position":[0,"one",0]}])",
          R"([{"name":"Spark","position":[0,1e100,0]}])",
          R"([{"name":"Spark","position":[0,-25,0]}])",
          R"([{"name":"Spark","position":[0,-26,0]}])",
          R"([{"name":"Spark","position":[0,1,0],"radius":0.09999}])",
          R"([{"name":"Spark","position":[0,1,0],"radius":2.00001}])",
          R"([{"name":"Spark","position":[0,1,0],"radius":null}])",
          R"([{"name":"Spark","position":[0,1,0],"radius":true}])",
          R"([{"name":"Spark","position":[0,1,0],"raduis":1}])",
          R"([{"name":"Spark","position":[0,1,0]},{"name":"Spark","position":[1,1,0]}])"}) {
        CAPTURE(sparks);
        const auto level = mj::game::parse_level(level_json(sparks));
        REQUIRE_FALSE(level);
        CHECK(level.error().message.find("level.collectibles") != std::string::npos);
    }
    CHECK_FALSE(mj::game::parse_level(
        level_json(R"([{"name":"Spark","name":"Again","position":[0,1,0]}])")));
    CHECK_FALSE(mj::game::parse_level(level_json(R"([{"name":"Spark","position":[0,1e999,0]}])")));
    CHECK_FALSE(mj::game::parse_level(
        level_json("[{\"name\":\"" + std::string(81, 'a') + "\",\"position\":[0,1,0]}]")));
}

TEST_CASE("spark pickups use the whole capsule and rounded caps, collecting overlaps once") {
    const auto level = airborne_level({{"Head", {0, 11.65F, 0}, 0.1F},
                                       {"Side", {0.44F, 10.8F, 0}, 0.1F},
                                       {"Outside rounded head", {0.4F, 11.6F, 0}, 0.1F},
                                       {"Feet", {0, 9.96F, 0}, 0.1F},
                                       {"Below", {0, 9.5F, 0}, 0.1F}});
    const mj::CollisionWorld world;
    Simulation simulation(level, world);
    no_gravity(simulation);
    CHECK(simulation.collectible_count() == 5);
    CHECK(simulation.collected_count() == 0);
    CHECK_FALSE(simulation.last_collected());
    CHECK(simulation.last_collection_tick() == 0);
    simulation.tick({}, k_dt);
    CHECK(simulation.collectible_collected(0));
    CHECK(simulation.collectible_collected(1));
    CHECK_FALSE(simulation.collectible_collected(2));
    CHECK(simulation.collectible_collected(3));
    CHECK_FALSE(simulation.collectible_collected(4));
    CHECK_FALSE(simulation.collectible_collected(5));
    CHECK(simulation.collected_count() == 3);
    CHECK(simulation.last_collected() == 3);
    CHECK(simulation.last_collection_tick() == 1);
    for (int i = 0; i < 10; ++i)
        simulation.tick({}, k_dt);
    CHECK(simulation.collected_count() == 3);
    CHECK(simulation.last_collection_tick() == 1);

    const auto tall_level = airborne_level({{"Tall head", {0, 13.05F, 0}, 0.1F}});
    Simulation tall(tall_level, world);
    no_gravity(tall);
    tall.player.tuning.radius = 0.2F;
    tall.player.tuning.height = 3;
    tall.tick({}, k_dt);
    CHECK(tall.collected_count() == 1);
}

TEST_CASE("swept capsule picks up sparks passed between ticks, including vertical motion") {
    const mj::CollisionWorld world;
    const auto level = airborne_level(
        {{"Passed sideways", {5, 10.8F, 0}, 0.1F}, {"Missed sideways", {5, 10.8F, 0.6F}, 0.1F}});
    Simulation simulation(level, world);
    no_gravity(simulation);
    simulation.player.tuning.air_acceleration = 10;
    simulation.player.tuning.air_speed = 10;
    simulation.tick({1, 0, 0, 0, 0}, 1);
    REQUIRE(simulation.player.current().position.x > 9.0F);
    CHECK(simulation.collectible_collected(0));
    CHECK_FALSE(simulation.collectible_collected(1));

    const auto falling_level = airborne_level(
        {{"Passed falling", {0, 5, 0}, 0.1F}, {"Missed falling", {0.6F, 5, 0}, 0.1F}});
    Simulation falling(falling_level, world);
    falling.tick({}, 0.5F);
    REQUIRE(falling.player.current().position.y < 0.0F);
    CHECK(falling.collectible_collected(0));
    CHECK_FALSE(falling.collectible_collected(1));
}

TEST_CASE("solid walls block spark reach and a fast approach cannot collect the far side") {
    auto level =
        airborne_level({{"Near side", {4, 10.8F, 0}, 0.1F}, {"Far side", {5.2F, 10.8F, 0}, 0.65F}});
    level.shapes.push_back(
        {mj::game::ShapeKind::box, {5, 10, 0}, {0.1F, 10, 10}, {1, 1, 1}, 0, {}});
    const auto geometry = mj::game::build_geometry(level);
    Simulation simulation(level, geometry.collision);
    no_gravity(simulation);
    simulation.player.tuning.air_acceleration = 10;
    simulation.player.tuning.air_speed = 10;
    simulation.tick({1, 0, 0, 0, 0}, 1);
    REQUIRE(simulation.player.current().position.x < 4.7F);
    CHECK(simulation.collectible_collected(0));
    CHECK_FALSE(simulation.collectible_collected(1));
}

TEST_CASE("spark progress survives respawn, reset and death without collecting teleports") {
    const auto level = airborne_level({{"Keep", {0, 10.8F, 0}},
                                       {"Between spawn and reset", {50, 10.8F, 0}},
                                       {"At death", {100, -25.2F, 0}}});
    const mj::CollisionWorld world;
    Simulation simulation(level, world);
    no_gravity(simulation);
    simulation.tick({}, k_dt);
    REQUIRE(simulation.collected_count() == 1);
    simulation.player.place({100, 10, 0}, 0);
    simulation.tick({0, 0, 0, 0, mj::input_button::reset_pressed}, k_dt);
    CHECK(simulation.respawns() == 1);
    CHECK(simulation.collected_count() == 1);
    CHECK_FALSE(simulation.collectible_collected(1));
    simulation.player.place({100, -26, 0}, 0);
    simulation.tick({}, k_dt);
    CHECK(simulation.respawns() == 2);
    CHECK(simulation.collected_count() == 1);
    CHECK_FALSE(simulation.collectible_collected(2));
    simulation.respawn();
    CHECK(simulation.collected_count() == 1);
    CHECK(simulation.last_collection_tick() == 1);
    Simulation fresh(level, world);
    CHECK(fresh.collected_count() == 0);
    CHECK_FALSE(fresh.collectible_collected(0));
    CHECK_FALSE(fresh.last_collected());
    CHECK(fresh.last_collection_tick() == 0);
}

TEST_CASE("spark progress is identical across render cadences and leaves movement unchanged") {
    auto parsed = mj::game::parse_level(level_json(R"([
        {"name":"Start","position":[0,1,0]},
        {"name":"Run","position":[0,1,-6]},
        {"name":"Distant","position":[30,1,0]}
    ])"));
    REQUIRE(parsed);
    const auto &level = *parsed;
    auto plain = level;
    plain.collectibles.clear();
    const auto geometry = mj::game::build_geometry(level);
    Simulation source(level, geometry.collision);
    Simulation original(plain, geometry.collision);
    std::vector<Simulation> expected;
    for (int tick = 0; tick < 240; ++tick) {
        const mj::TickInput input{0, 1, 0, -0.32F, 0};
        source.tick(input, k_dt);
        original.tick(input, k_dt);
        const auto &a = source.player.current();
        const auto &b = original.player.current();
        CHECK(same(a.position.x, b.position.x));
        CHECK(same(a.position.y, b.position.y));
        CHECK(same(a.position.z, b.position.z));
        CHECK(same(a.velocity.x, b.velocity.x));
        CHECK(same(a.velocity.y, b.velocity.y));
        CHECK(same(a.velocity.z, b.velocity.z));
        CHECK(source.active_checkpoint() == original.active_checkpoint());
        CHECK(source.respawns() == original.respawns());
        CHECK(same(source.camera.distance(), original.camera.distance()));
        expected.push_back(source);
    }
    REQUIRE(source.collected_count() == 2);
    for (const auto &cadence : std::vector<std::vector<double>>{
             {1.0 / 30}, {1.0 / 60}, {1.0 / 144}, {1.0 / 240}, {0.004, 0.019, 0.011, 0.031}}) {
        Simulation replay(level, geometry.collision);
        mj::FixedStep step;
        for (std::size_t frame = 0; replay.ticks() < expected.size(); ++frame) {
            step.advance(cadence[frame % cadence.size()], [&](float dt) {
                if (replay.ticks() >= expected.size())
                    return;
                replay.tick({0, 1, 0, -0.32F, 0}, dt);
                const auto &reference = expected[replay.ticks() - 1];
                CHECK(replay.collected_count() == reference.collected_count());
                CHECK(replay.last_collected() == reference.last_collected());
                CHECK(replay.last_collection_tick() == reference.last_collection_tick());
                for (std::size_t i = 0; i < level.collectibles.size(); ++i)
                    CHECK(replay.collectible_collected(i) == reference.collectible_collected(i));
            });
        }
    }
}
