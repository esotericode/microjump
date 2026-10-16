#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace {
constexpr float k_dt = 1.0F / 120;

std::size_t spark_index(const mj::game::Level &level, std::string_view name) {
    for (std::size_t i = 0; i < level.collectibles.size(); ++i) {
        if (level.collectibles[i].name == name)
            return i;
    }
    FAIL("Missing authored spark: " << name);
    return level.collectibles.size();
}
} // namespace

TEST_CASE("every playground spark has a walkable landing and standing pickup clearance") {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    REQUIRE(level->collectibles.size() == 16);
    const auto geometry = mj::game::build_geometry(*level);
    const mj::game::PlayerTuning tuning;
    for (std::size_t i = 0; i < level->collectibles.size(); ++i) {
        const auto &spark = level->collectibles[i];
        CAPTURE(spark.name);
        const auto support = geometry.collision.raycast({spark.position, {0, -1, 0}, 2});
        REQUIRE(support);
        CHECK(mj::is_walkable(support->normal, tuning.collision));
        CHECK(support->distance > 0.8F);
        CHECK(support->distance < 1.2F);
        // A centre ray alone can accept a thin pole or the very edge of a landing.
        // Check that the intended standing position has room for the whole capsule.
        for (const auto offset : std::array<mj::Vec3, 4>{{{tuning.radius, 0, 0},
                                                          {-tuning.radius, 0, 0},
                                                          {0, 0, tuning.radius},
                                                          {0, 0, -tuning.radius}}}) {
            const auto nearby =
                geometry.collision.raycast({spark.position + offset, {0, -1, 0}, 2});
            REQUIRE(nearby);
            CHECK(mj::is_walkable(nearby->normal, tuning.collision));
            CHECK(std::abs(nearby->point.y - support->point.y) < 0.08F);
        }
        mj::game::Simulation simulation(*level, geometry.collision);
        simulation.player.place(support->point + mj::Vec3{0, 0.02F, 0}, 0);
        for (int tick = 0; tick < 60; ++tick)
            simulation.tick({}, k_dt);
        CHECK(simulation.player.current().grounded);
        CHECK(mj::length(simulation.player.current().position - support->point) < 0.04F);
        CHECK(mj::length(geometry.collision.depenetration(simulation.player.capsule(),
                                                          tuning.collision.skin)) < 0.001F);
        CHECK(simulation.collectible_collected(i));
        CHECK(simulation.respawns() == 0);
    }
}

TEST_CASE("the first playground spark invites a short walk instead of collecting on spawn") {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);
    mj::game::Simulation simulation(*level, geometry.collision);
    for (int tick = 0; tick < 60; ++tick)
        simulation.tick({}, k_dt);
    CHECK(simulation.collected_count() == 0);
    for (int tick = 0; tick < 100; ++tick)
        simulation.tick({0, 1, 0, -0.32F, 0}, k_dt);
    CHECK(simulation.collectible_collected(spark_index(*level, "First light")));
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("the playground sprint and stairs lead naturally through their spark rewards") {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);

    SUBCASE("sprint: a straight run reaches the midpoint and far finish") {
        mj::game::Simulation simulation(*level, geometry.collision);
        simulation.player.place({7, 0.07F, -5}, 0);
        for (int tick = 0; tick < 1410; ++tick)
            simulation.tick({0, 1, 0, -0.32F, 0}, k_dt);
        CHECK(simulation.collectible_collected(spark_index(*level, "Second wind")));
        CHECK(simulation.collectible_collected(spark_index(*level, "Last light")));
        CHECK(simulation.player.current().grounded);
        CHECK(simulation.respawns() == 0);
    }
    SUBCASE("little stairs: climb at a walk then cross the landing") {
        mj::game::Simulation simulation(*level, geometry.collision);
        simulation.player.place({16, 0.01F, 3}, mj::k_pi);
        for (int tick = 0; tick < 300; ++tick)
            simulation.tick({0, 0.4F, mj::k_pi, -0.32F, 0}, k_dt);
        CHECK(simulation.collectible_collected(spark_index(*level, "Little stair")));
        CHECK(simulation.respawns() == 0);
    }
    SUBCASE("summit: the stair course and a short turn reach the lookout") {
        mj::game::Simulation simulation(*level, geometry.collision);
        simulation.player.place({63, 0.01F, 28}, mj::k_pi);
        for (int tick = 0; tick < 600; ++tick)
            simulation.tick({0, 0.4F, mj::k_pi, -0.32F, 0}, k_dt);
        for (int tick = 0; tick < 160; ++tick) {
            const auto toward = mj::normalized(
                mj::horizontal(mj::Vec3{67, 0, 46} - simulation.player.current().position));
            simulation.tick({toward.x * 0.4F, -toward.z * 0.4F, 0, -0.32F, 0}, k_dt);
        }
        CHECK(simulation.collectible_collected(spark_index(*level, "Summit sun")));
        CHECK(simulation.player.current().position.y > 5.9F);
        CHECK(simulation.respawns() == 0);
    }
}

TEST_CASE("all four narrowing playground routes can be jumped to their final spark") {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);
    constexpr std::array routes{std::pair{-48.0F, "Broad strides"},
                                std::pair{-62.0F, "Steady steps"}, std::pair{-76.0F, "Narrow way"},
                                std::pair{-90.0F, "Needle thread"}};
    for (const auto &[x, name] : routes) {
        CAPTURE(name);
        mj::game::Simulation simulation(*level, geometry.collision);
        simulation.player.place({x, 0.01F, -7.1F}, 0);
        const auto target = spark_index(*level, name);
        int jump_number = 0;
        for (int tick = 0; tick < 1800 && !simulation.collectible_collected(target); ++tick) {
            const auto &state = simulation.player.current();
            // Land, run across the platform, then jump from near its front edge.
            // Jumping again immediately on landing eventually falls short of a gap.
            const float takeoff =
                jump_number == 0 ? -7.0F : -11.1F - 6.0F * static_cast<float>(jump_number - 1);
            const auto jump =
                state.grounded && state.position.z <= takeoff ? mj::input_button::jump_pressed : 0U;
            if (jump != 0)
                ++jump_number;
            simulation.tick({0, 0.75F, 0, -0.32F, jump | mj::input_button::jump_held}, k_dt);
        }
        CHECK(simulation.collectible_collected(target));
        CHECK(jump_number == 8);
        CHECK(simulation.player.current().position.y > 4.5F);
        CHECK(simulation.respawns() == 0);
    }
}
