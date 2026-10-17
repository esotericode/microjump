#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <mj/scene/mesh.hpp>

#include <cmath>
#include <numbers>
#include <string>

namespace {
namespace button = mj::input_button;
constexpr float k_dt = 1.0F / 120;
using mj::Vec3;

// A floor far below, so only movers hold the player up, plus the given movers.
std::string level_with(std::string_view movers) {
    return R"({"version": 1, "name": "Mover test", "kill_height": -40,
      "shapes": [{"kind":"box", "center":[0,-20.5,0], "size":[200,1,200], "color":"#808080"}],
      "checkpoints": [{"position":[0,-19.99,0]}],
      "movers": [)" +
           std::string(movers) + "]}";
}

mj::game::Level parsed(std::string_view movers) {
    auto level = mj::game::parse_level(level_with(movers));
    if (!level)
        FAIL(level.error().message);
    return *level;
}

std::string error_of(std::string_view movers) {
    const auto level = mj::game::parse_level(level_with(movers));
    return level ? std::string{} : level.error().message;
}

// A 4 m slab whose top is at the pivot's height.
constexpr std::string_view k_slab =
    R"({"kind":"box", "center":[0,-0.25,0], "size":[4,0.5,4], "color":"#b08d57"})";

std::string mover(std::string_view motion, Vec3 pivot = {0, 0, 0},
                  std::string_view shape = k_slab) {
    return R"({"name":"M", "pivot":[)" + std::to_string(pivot.x) + "," + std::to_string(pivot.y) +
           "," + std::to_string(pivot.z) + R"(], "shapes":[)" + std::string(shape) + "], " +
           std::string(motion) + "}";
}

struct Run {
    mj::game::Level level;
    mj::game::LevelGeometry geometry;
    mj::game::Simulation simulation;
    explicit Run(mj::game::Level loaded, Vec3 start, float facing = 0)
        : level(std::move(loaded)), geometry(mj::game::build_geometry(level)),
          simulation(level, geometry.collision) {
        simulation.player.place(start, facing);
    }
    void hold(mj::TickInput input, int ticks) {
        for (int i = 0; i < ticks; ++i)
            simulation.tick(input, k_dt);
    }
    [[nodiscard]] const mj::game::PlayerState &state() const {
        return simulation.player.current();
    }
    // The player's feet in the mover's space.
    [[nodiscard]] Vec3 on_mover() const {
        return mj::to_local(simulation.mover_pose(0, 1), state().position);
    }
};

} // namespace

TEST_CASE("a scene finds moving bodies at their poses") {
    mj::SceneMesh slab;
    slab.add_box({{0, -0.25F, 0}, {8, 0.5F, 1}, {1, 1, 1}}); // Long along body X.
    mj::CollisionWorld world;
    world.add_body(slab.triangles(), {{0, 0, 0}, 0});
    const std::array poses{mj::BodyPose{{10, 2, 0}, std::numbers::pi_v<float> / 2}};
    const mj::CollisionScene scene(world, {poses, poses});

    // Turned a quarter, the slab runs along world Z through (10, 2, 0).
    const auto down = scene.raycast({{10, 5, 3}, {0, -1, 0}, 10});
    REQUIRE(down);
    CHECK(down->body == 0U);
    CHECK(down->distance == doctest::Approx(3));
    CHECK(down->normal.y == doctest::Approx(1));
    CHECK_FALSE(scene.raycast({{13, 5, 0}, {0, -1, 0}, 10}));
    CHECK_FALSE(world.raycast({{10, 5, 3}, {0, -1, 0}, 10})); // The world alone is static.

    const mj::Capsule capsule{{10, 4, -3}};
    const auto hit = scene.sweep({capsule, {0, -4, 0}});
    REQUIRE(hit);
    CHECK(hit->body == 0U);
    CHECK(capsule.base.y - 4 * hit->fraction == doctest::Approx(2).epsilon(0.01));

    const auto push = scene.depenetration({{10, 1.9F, 0}}, 0.01F);
    CHECK(push.y > 0.09F);
    // Without poses, a scene places each body where it rests.
    CHECK(mj::CollisionScene(world).raycast({{3, 5, 0}, {0, -1, 0}, 10}));
}

TEST_CASE("mover poses follow eased paths, pauses, loops, orbits and spins") {
    using mj::game::mover_pose;
    auto level = parsed(mover(R"("path":[[0,0,0],[10,0,0]], "period":4)"));
    const auto &slide = level.movers[0];
    CHECK(mover_pose(slide, 0).position.x == doctest::Approx(0));
    CHECK(mover_pose(slide, 1).position.x == doctest::Approx(5));
    CHECK(mover_pose(slide, 2).position.x == doctest::Approx(10));
    CHECK(mover_pose(slide, 3).position.x == doctest::Approx(5));
    CHECK(mover_pose(slide, 4).position.x == doctest::Approx(0).epsilon(1e-4));
    CHECK(mover_pose(slide, 0.1).position.x < 0.5F * 0.1F * 5); // Eases out of each stop.

    level = parsed(mover(R"("path":[[0,0,0],[0,6,0]], "period":10, "pause":1, "phase":0.5)"));
    CHECK(mover_pose(level.movers[0], 0).position.y == doctest::Approx(6));   // Half a cycle on.
    CHECK(mover_pose(level.movers[0], 0.9).position.y == doctest::Approx(6)); // Pausing there.
    CHECK(mover_pose(level.movers[0], 3).position.y == doctest::Approx(3));   // Halfway down.

    level = parsed(mover(R"("path":[[0,0,0],[4,0,0],[4,0,4]], "loop":true, "period":12)"));
    // Legs of 4, 4 and 5.66 m share the period by length.
    const double total = 8 + 4 * std::numbers::sqrt2;
    CHECK(mover_pose(level.movers[0], 12 * 4 / total).position.x == doctest::Approx(4));
    CHECK(mover_pose(level.movers[0], 12 * 8 / total).position.z == doctest::Approx(4));

    level = parsed(mover(R"("orbit":{"radius":5, "upright":true}, "period":8)", {0, 10, 0}));
    CHECK(mover_pose(level.movers[0], 0).position.y == doctest::Approx(5));
    CHECK(mover_pose(level.movers[0], 2).position.x == doctest::Approx(5));
    CHECK(mover_pose(level.movers[0], 4).position.y == doctest::Approx(15));
    CHECK(mover_pose(level.movers[0], 2).yaw == doctest::Approx(0)); // Gondolas stay level.

    level = parsed(mover(R"("orbit":{"radius":5}, "period":8)"));
    CHECK(mover_pose(level.movers[0], 0).position.z == doctest::Approx(-5));
    CHECK(mover_pose(level.movers[0], 2).position.x == doctest::Approx(5));

    level = parsed(mover(R"("spin":0.5)"));
    CHECK(mover_pose(level.movers[0], 2).yaw == doctest::Approx(1));
    CHECK(std::abs(mover_pose(level.movers[0], 3600).yaw) <= std::numbers::pi_v<float>);
}

TEST_CASE("mover data is strict") {
    CHECK(error_of(mover(R"("spin":0.5)")).empty());
    CHECK(error_of(mover(R"("period":4)")).find("needs a path, an orbit or a spin") !=
          std::string::npos);
    CHECK(error_of(mover(R"("path":[[0,0,0],[4,0,0]])")).find("needs a period") !=
          std::string::npos);
    CHECK(error_of(mover(R"("spin":1, "period":4)")).find("only a path or orbit") !=
          std::string::npos);
    CHECK(error_of(mover(R"("path":[[1,0,0],[1,0,0]], "period":4)")).find("two different") !=
          std::string::npos);
    CHECK(error_of(mover(R"("path":[[0,0,0],[4,0,0]], "period":4, "pause":1.9)"))
              .find("too little") != std::string::npos);
    CHECK(error_of(mover(R"("path":[[0,0,0],[40,0,0]], "period":4)")).find("20 m/s") !=
          std::string::npos);
    CHECK(error_of(mover(R"("spin":8)")).find("20 m/s") != std::string::npos);
    CHECK(error_of(mover(R"("spin":1, "speed":2)")).find("movers[0].speed: unknown field") !=
          std::string::npos);
    CHECK(error_of(mover(R"("spin":1)", {},
                         R"({"kind":"box","center":[0,0,0],"size":[1,1,1],"color":"#ffffff",
                            "solid":false})"))
              .find("at least one solid shape") != std::string::npos);
    CHECK(error_of(mover(R"("spin":1)") + "," + mover(R"("spin":1)"))
              .find("mover names must be unique") != std::string::npos);
    // Levels without movers are unchanged.
    CHECK(mj::game::parse_level(R"({"version":1,"name":"N","shapes":[
        {"kind":"box","center":[0,0,0],"size":[1,1,1],"color":"#ffffff"}],
        "checkpoints":[{"position":[0,1,0]}]})"));
}

TEST_CASE("a sliding platform carries its rider through a whole cycle") {
    Run run(parsed(mover(R"("path":[[0,0,0],[12,0,0]], "period":6, "pause":0.5)")),
            {0.5F, 0.01F, 0.5F});
    run.hold({}, 30);
    REQUIRE(run.state().grounded);
    REQUIRE(run.state().ground_body == 0U);
    const Vec3 spot = run.on_mover();
    for (int i = 0; i < 6 * 120; ++i) {
        run.hold({}, 1);
        REQUIRE(run.state().grounded);
        REQUIRE(std::abs(run.on_mover().x - spot.x) < 0.02F);
        REQUIRE(std::abs(run.on_mover().z - spot.z) < 0.02F);
        REQUIRE(std::abs(run.on_mover().y - spot.y) < 0.02F);
    }
}

TEST_CASE("a lift raises and lowers its rider without losing the ground") {
    Run run(parsed(mover(R"("path":[[0,0,0],[0,8,0]], "period":8, "pause":1)")), {0, 0.01F, 0});
    run.hold({}, 30);
    float highest = 0;
    for (int i = 0; i < 8 * 120; ++i) {
        run.hold({}, 1);
        REQUIRE(run.state().grounded);
        highest = std::max(highest, run.state().position.y);
    }
    CHECK(highest == doctest::Approx(8).epsilon(0.01));
    CHECK(run.state().position.y == doctest::Approx(0).scale(1).epsilon(0.02));
}

TEST_CASE("a turntable carries its rider around and turns them with it") {
    Run run(parsed(mover(R"("spin":0.5)", {}, R"({"kind":"cylinder","center":[0,-0.25,0],
                                                 "size":[10,0.5,10],"color":"#b08d57"})")),
            {3, 0.01F, 0}, std::numbers::pi_v<float>);
    run.hold({}, 1);
    // Facing the way the rim moves, a drop lands standing still on it, without turning.
    CHECK(run.state().ground_speed == 0.0F);
    const Vec3 start = run.state().position;
    const float facing = run.state().facing;
    run.hold({}, 120 * 2); // Two more seconds: one radian further round.
    CHECK(run.state().grounded);
    const Vec3 expected = mj::rotate_yaw(start, 1);
    CHECK(std::abs(run.state().position.x - expected.x) < 0.02F);
    CHECK(std::abs(run.state().position.z - expected.z) < 0.02F);
    CHECK(mj::angle_difference(facing, run.state().facing) == doctest::Approx(1).epsilon(0.01));
}

TEST_CASE("a hop on a moving platform lands where it took off") {
    Run run(parsed(mover(R"("path":[[0,0,0],[30,0,0]], "period":12)")), {0, 0.01F, 0});
    run.hold({}, 120 * 2); // Moving well by now (about 6 m/s).
    const Vec3 spot = run.on_mover();
    run.hold({0, 0, 0, 0, button::jump_pressed | button::jump_held}, 1);
    REQUIRE_FALSE(run.state().grounded);
    CHECK(mj::length(mj::horizontal(run.state().velocity)) > 5.0F);
    while (!run.state().grounded)
        run.hold({0, 0, 0, 0, button::jump_held}, 1);
    CHECK(run.state().ground_body == 0U);
    // The platform eases a little between takeoff and landing; the hop stays close.
    CHECK(mj::length(mj::horizontal(run.on_mover() - spot)) < 0.6F);
    run.hold({}, 30);
    CHECK(run.state().ground_speed < 0.5F); // No phantom speed from the platform.
}

TEST_CASE("jumping off a moving platform keeps its velocity") {
    Run run(parsed(mover(R"("path":[[0,0,0],[30,0,0]], "period":12)")), {0, 0.01F, 0});
    run.hold({}, 120 * 3); // Near top speed, moving +X.
    const auto carried = mj::game::mover_pose(run.level.movers[0], run.simulation.seconds());
    const auto before = mj::game::mover_pose(run.level.movers[0], run.simulation.seconds() - k_dt);
    const float platform_speed = (carried.position.x - before.position.x) / k_dt;
    run.hold({0, 0, 0, 0, button::jump_pressed | button::jump_held}, 1);
    CHECK(run.state().velocity.x == doctest::Approx(platform_speed).epsilon(0.05));
}

TEST_CASE("a mover pushes a player standing in its way") {
    auto level =
        parsed(mover(R"("path":[[0,0,0],[0,0,-8]], "period":8)", {0, -19, 6},
                     R"({"kind":"box","center":[0,0,0],"size":[6,2,1],"color":"#704020"})"));
    Run run(std::move(level), {0, -19.99F, 4});
    run.hold({}, 30);
    float closest = -100; // Feet relative to the wall's face; -0.35 is touching.
    for (int i = 0; i < 4 * 120; ++i) {
        run.hold({}, 1);
        const float face = run.simulation.mover_pose(0, 1).position.z - 0.5F;
        closest = std::max(closest, run.state().position.z - face);
    }
    CHECK(closest < -0.3F);                // At most one tick of overlap before the push.
    CHECK(run.state().position.z < -2.5F); // Shoved all the way, standing on the floor.
    CHECK(run.state().grounded);
}

TEST_CASE("movers keep simulations reproducible") {
    const auto level = parsed(mover(R"("path":[[0,0,0],[8,3,0]], "period":5, "spin":0.3)"));
    Run a(level, {0.5F, 0.01F, 0});
    Run b(level, {0.5F, 0.01F, 0});
    for (int i = 0; i < 600; ++i) {
        const mj::TickInput input{0.4F, 0.6F, 0.2F * static_cast<float>(i % 7), 0,
                                  i % 90 == 0 ? button::jump_pressed : 0U};
        a.hold(input, 1);
        b.hold(input, 1);
    }
    CHECK(a.state().position.x == b.state().position.x);
    CHECK(a.state().position.y == b.state().position.y);
    CHECK(a.state().position.z == b.state().position.z);
    CHECK(a.state().facing == b.state().facing);
}
