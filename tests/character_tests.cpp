#include "test_framework.hpp"

#include <character.hpp>
#include <level_io.hpp>
#include <simulation.hpp>

#include <mj/core/input_record.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using mj::Vec3;
using mj::game::CharacterAnimator;
using mj::game::CharacterPart;

namespace {
constexpr float k_dt = 1.0F / 120;

float determinant(const mj::Mat4 &m) {
    const auto at = [&](std::size_t row, std::size_t column) { return m.at(row, column); };
    return at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
           at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
           at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
}
Vec3 origin(const mj::Mat4 &m) {
    return {m.at(0, 3), m.at(1, 3), m.at(2, 3)};
}
mj::CollisionWorld floor_world() {
    mj::SceneMesh mesh;
    mesh.add_box({{0, -0.5F, 0}, {200, 1, 200}, {1, 1, 1}});
    return mj::CollisionWorld(mesh.triangles());
}

// The highlights of a replay, as the animator saw them.
struct Seen {
    float deepest_squash = 1;
    float highest_stretch = 1;
    float deepest_dive = 0;
    float largest_spin = 0;
    float brightest_glow = 0;
    float largest_stride = 0;
    std::size_t most_parts = 0;
};

Seen animate_replay(const std::string &file) {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto replay = mj::load_replay(std::filesystem::path(MJ_TEST_REPLAYS) / file);
    REQUIRE(replay);
    const auto geometry = mj::game::build_geometry(*level);
    mj::game::Simulation simulation(*level, geometry.collision);
    CharacterAnimator character;
    character.snap(simulation.player.current());
    Seen seen;
    std::vector<CharacterPart> parts;
    for (const auto &input : *replay) {
        simulation.tick(input, k_dt);
        character.tick(simulation.player.current(), k_dt);
        const auto &pose = character.current();
        seen.deepest_squash = std::min(seen.deepest_squash, pose.squash);
        seen.highest_stretch = std::max(seen.highest_stretch, pose.squash);
        seen.deepest_dive = std::max(seen.deepest_dive, pose.dive);
        seen.largest_spin = std::max(seen.largest_spin, pose.spin);
        seen.brightest_glow = std::max(seen.brightest_glow, pose.glow);
        seen.largest_stride = std::max(seen.largest_stride, pose.stride);
        for (const float alpha : {0.0F, 0.5F, 1.0F}) {
            const auto feet = simulation.player.interpolated(alpha);
            parts.clear();
            character.build(feet, alpha, parts);
            REQUIRE(parts.size() >= CharacterAnimator::k_body_parts);
            REQUIRE(parts.size() <=
                    CharacterAnimator::k_body_parts + CharacterAnimator::k_max_puffs);
            seen.most_parts = std::max(seen.most_parts, parts.size());
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const auto &model = parts[i].model;
                for (const float value : model.values)
                    REQUIRE(std::isfinite(value));
                // Never mirrored or flattened, so the shader's cofactor normals stay outward.
                REQUIRE(determinant(model) > 0.0F);
                if (i >= CharacterAnimator::k_body_parts)
                    continue; // Dust drifts away from the body on purpose.
                // Every body part stays with the collision capsule it stands in for.
                const Vec3 offset = origin(model) - feet.position;
                CAPTURE(simulation.ticks());
                CAPTURE(i);
                CHECK(std::hypot(offset.x, offset.z) < 1.1F);
                CHECK(offset.y > -0.45F);
                CHECK(offset.y < 2.0F);
            }
        }
    }
    return seen;
}
} // namespace

TEST_CASE("the character performs the committed move tours without leaving its capsule") {
    const auto tour = animate_replay("m4-moves-tour.mjr");
    CHECK(tour.largest_stride > 0.99F);  // A full run cycle.
    CHECK(tour.deepest_dive > 0.9F);     // The long jump's dive.
    CHECK(tour.largest_spin > 8.5F);     // Wall kicks twirl (3 pi, less a tick of easing).
    CHECK(tour.deepest_squash < 0.92F);  // Landings squash...
    CHECK(tour.highest_stretch > 1.05F); // ...and takeoffs stretch.
    CHECK(tour.most_parts > CharacterAnimator::k_body_parts); // Dust was kicked up.
    // Clean hops at speed flash the antenna, so kept momentum is visible.
    const auto chain = animate_replay("m4-momentum-chain.mjr");
    CHECK(chain.brightest_glow > 0.9F);
}

TEST_CASE("landings squash with the fall height, then the spring settles") {
    const auto world = floor_world();
    const auto drop = [&](float height) {
        mj::game::Player player;
        player.place({0, height, 0}, 0);
        CharacterAnimator character;
        character.snap(player.current());
        float deepest = 1;
        for (int i = 0; i < 240; ++i) {
            player.tick({}, world, k_dt);
            character.tick(player.current(), k_dt);
            deepest = std::min(deepest, character.current().squash);
        }
        // Two seconds in, only the gentle idle breathing remains.
        CHECK(std::abs(character.current().squash - 1) < 0.02F);
        CHECK(character.current().air == 0.0F);
        return deepest;
    };
    const float step = drop(0.3F);
    const float jump = drop(2.5F);
    const float tower = drop(12.0F);
    CHECK(step > 0.9F);
    CHECK(jump < step);
    CHECK(tower < jump);
    CHECK(tower >= 0.62F); // Clamped: the character never folds flat.
}

TEST_CASE("respawns snap the character instead of smearing it across the level") {
    const auto world = floor_world();
    mj::game::Player player;
    player.place({0, 0, 0}, 0);
    CharacterAnimator character;
    character.snap(player.current());
    for (int i = 0; i < 120; ++i) {
        player.tick({0, 1, 0, 0, 0}, world, k_dt); // Run, then respawn far away.
        character.tick(player.current(), k_dt);
    }
    REQUIRE(character.current().stride > 0.5F);
    player.place({40, 5, 40}, 1);
    player.tick({}, world, k_dt);
    character.tick(player.current(), k_dt);
    CHECK(character.current().stride == 0.0F);
    std::vector<CharacterPart> before, after;
    const auto pose = player.interpolated(1);
    character.build(pose, 0, before);
    character.build(pose, 1, after);
    REQUIRE(before.size() == after.size());
    for (std::size_t i = 0; i < before.size(); ++i)
        CHECK(before[i].model.values == after[i].model.values);
}
