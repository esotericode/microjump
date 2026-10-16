#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <mj/core/fixed_step.hpp>

#include <bit>
#include <filesystem>

namespace {
namespace button = mj::input_button;
constexpr float k_dt = 1.0F / 120;
using mj::TickInput;
using mj::Vec3;
using mj::game::MoveState;

mj::CollisionWorld wall_world(bool marked, bool floor = false) {
    mj::SceneMesh mesh;
    mesh.add_box({{2.5F, 0, 0}, {1, 100, 20}, {1, 1, 1}});
    auto triangles = mesh.triangles();
    for (auto &triangle : triangles)
        triangle.wall_jump = marked;
    // Degenerate filtering must not shift surface designations onto another triangle.
    triangles.insert(triangles.begin(), mj::Triangle{});
    if (floor) {
        mj::SceneMesh ground;
        ground.add_box({{0, -0.5F, 0}, {200, 1, 200}, {1, 1, 1}});
        const auto floor_triangles = ground.triangles();
        triangles.insert(triangles.end(), floor_triangles.begin(), floor_triangles.end());
    }
    return mj::CollisionWorld(triangles);
}

mj::game::Player at_wall(const mj::CollisionWorld &world) {
    mj::game::Player player;
    player.place({1.5F, 20, 0}, mj::k_pi * 0.5F);
    for (int i = 0; i < 25; ++i)
        player.tick({1, 0, 0, 0, 0}, world, k_dt);
    return player;
}

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
bool same(Vec3 a, Vec3 b) {
    return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}
void identical(const mj::game::Simulation &a, const mj::game::Simulation &b) {
    const auto &p = a.player.current();
    const auto &q = b.player.current();
    CHECK(a.ticks() == b.ticks());
    CHECK(same(p.position, q.position));
    CHECK(same(p.velocity, q.velocity));
    CHECK(same(p.facing, q.facing));
    CHECK(p.state == q.state);
    CHECK(p.grounded == q.grounded);
    CHECK(same(p.wall_normal, q.wall_normal));
    CHECK(same(p.last_kick_normal, q.last_kick_normal));
    CHECK(same(p.wall_timer, q.wall_timer));
    CHECK(same(p.recovery_timer, q.recovery_timer));
    CHECK(p.wall_used == q.wall_used);
    CHECK(same(a.camera.focus(), b.camera.focus()));
    CHECK(same(a.camera.distance(), b.camera.distance()));
}
} // namespace

TEST_CASE("wall kicks require a marked vertical face and work with grace disabled") {
    for (const bool marked : {false, true}) {
        const auto world = wall_world(marked);
        for (const bool grace : {false, true}) {
            auto player = at_wall(world);
            CHECK(player.current().position.x < 1.66F);
            if (!grace) {
                player.tuning.wall_contact_grace = 0;
                player.tuning.jump_buffer = 0;
                player.tick({1, 0, 0, 0, 0}, world, k_dt);
            }
            player.tick({1, 0, 0, 0, button::jump_pressed | button::jump_held}, world, k_dt);
            CHECK((player.current().state == MoveState::wall_jump) == marked);
            if (marked) {
                CHECK(player.current().velocity.x < -8.0F);
                CHECK(player.current().velocity.y > 11.0F);
                CHECK(player.current().wall_used);
                const float start = player.current().position.x;
                for (int i = 0; i < 10; ++i)
                    player.tick({1, 0, 0, 0, button::jump_held}, world, k_dt);
                CHECK(player.current().position.x < start - 0.6F);
                CHECK(player.current().velocity.x < -8.0F);
            }
        }
    }
    // Even authored floor/ceiling tags must never become wall contacts.
    mj::game::Level level;
    level.shapes.push_back(
        {mj::game::ShapeKind::box, {0, 4, 0}, {20, 1, 20}, {1, 1, 1}, 0, {}, true});
    const auto geometry = mj::game::build_geometry(level);
    mj::game::Player player;
    player.place({0, 6, 0}, 0);
    for (int i = 0; i < 120; ++i)
        player.tick({}, geometry.collision, k_dt);
    CHECK(player.current().grounded);
    CHECK(player.current().wall_timer == 0.0F);
}

TEST_CASE("wall contact expires and the same wall cannot grant repeated airborne boosts") {
    const auto world = wall_world(true);
    auto player = at_wall(world);
    REQUIRE(player.current().wall_timer > 0.0F);
    // Move away without a new contact, then let the optional contact window expire.
    for (int i = 0; i < 40; ++i)
        player.tick({-1, 0, 0, 0, 0}, world, k_dt);
    player.tick({-1, 0, 0, 0, button::jump_pressed | button::jump_held}, world, k_dt);
    CHECK(player.current().state != MoveState::wall_jump);

    player = at_wall(world);
    player.tick({1, 0, 0, 0, button::jump_pressed | button::jump_held}, world, k_dt);
    REQUIRE(player.current().state == MoveState::wall_jump);
    bool returned = false;
    for (int i = 0; i < 300; ++i) {
        const bool contact = player.current().wall_timer > 0;
        const float before = player.current().velocity.y;
        player.tick(
            {1, 0, 0, 0, contact ? button::jump_pressed | button::jump_held : button::jump_held},
            world, k_dt);
        if (contact) {
            returned = true;
            CHECK(player.current().velocity.y <= before);
            CHECK(player.current().state != MoveState::wall_jump);
            break;
        }
    }
    CHECK(returned);
    CHECK_FALSE(player.current().grounded);
    player.place({0, 20, 0}, 0);
    CHECK_FALSE(player.current().wall_used);
    CHECK(player.current().wall_timer == 0.0F);
}

TEST_CASE("long jump trades height for distance and requires grounded running speed") {
    mj::SceneMesh mesh;
    mesh.add_box({{0, -0.5F, 0}, {200, 1, 200}, {1, 1, 1}});
    const mj::CollisionWorld floor(mesh.triangles());
    auto running = [&] {
        mj::game::Player player;
        player.place({0, 0.01F, 0}, 0);
        for (int i = 0; i < 120; ++i)
            player.tick({0, 1, 0, 0, 0}, floor, k_dt);
        return player;
    };
    float normal_distance = 0, normal_apex = 0;
    for (const bool long_jump : {false, true}) {
        auto player = running();
        const Vec3 start = player.current().position;
        const auto modifier = long_jump ? button::long_jump_held : 0U;
        player.tick({0, 1, 0, 0, button::jump_pressed | button::jump_held | modifier}, floor, k_dt);
        CHECK(player.current().state == (long_jump ? MoveState::long_jump : MoveState::jump));
        float apex = 0;
        for (int i = 0; i < 180 && !player.current().grounded; ++i) {
            apex = std::max(apex, player.current().position.y - start.y);
            player.tick({0, 1, 0, 0, button::jump_held | modifier}, floor, k_dt);
        }
        REQUIRE(player.current().grounded);
        const float distance = start.z - player.current().position.z;
        if (!long_jump) {
            normal_distance = distance;
            normal_apex = apex;
        } else {
            CHECK(distance > normal_distance * 1.15F);
            CHECK(apex < normal_apex * 0.6F);
            CHECK(player.current().ground_speed > player.tuning.run_speed);
            // Immediate chained long jumps retain a bounded launch speed.
            player.tick(
                {0, 1, 0, 0, button::jump_pressed | button::jump_held | button::long_jump_held},
                floor, k_dt);
            CHECK(mj::length(mj::horizontal(player.current().velocity)) <=
                  player.tuning.run_speed * player.tuning.long_jump_speed_scale + 1e-4F);
        }
    }
    auto stopped = running();
    for (int i = 0; i < 120; ++i)
        stopped.tick({}, floor, k_dt);
    stopped.tick({0, 0, 0, 0, button::jump_pressed | button::jump_held | button::long_jump_held},
                 floor, k_dt);
    CHECK(stopped.current().state == MoveState::jump);
    auto airborne = running();
    airborne.place({0, 5, 0}, 0);
    airborne.tick({0, 1, 0, 0, button::jump_pressed | button::jump_held | button::long_jump_held},
                  floor, k_dt);
    CHECK(airborne.current().state == MoveState::fall);
    CHECK(airborne.current().velocity.y < 0.0F);
}

TEST_CASE("version 2 adds the momentum modifier without changing version 1 buttons") {
    REQUIRE(mj::parse_replay("microjump-replay 1\ntick-rate 120\n1 0 1 0 0 7\n"));
    CHECK_FALSE(mj::parse_replay("microjump-replay 1\ntick-rate 120\n1 0 1 0 0 8\n"));
    const auto v2 = mj::parse_replay("microjump-replay 2\ntick-rate 120\n1 0 1 0 0 11\n");
    REQUIRE(v2);
    CHECK(v2->front().held(button::long_jump_held));
    CHECK_FALSE(mj::parse_replay("microjump-replay 2\ntick-rate 120\n1 0 1 0 0 16\n"));
}

TEST_CASE("new moves replay identically at 30, 60, 144, 240 Hz and irregular render cadences") {
    mj::game::Level level;
    level.checkpoints.push_back({{0, 0.01F, 0}, mj::k_pi * 0.5F});
    level.shapes.push_back(
        {mj::game::ShapeKind::box, {0, -0.5F, 0}, {200, 1, 200}, {1, 1, 1}, 0, {}});
    level.shapes.push_back(
        {mj::game::ShapeKind::box, {10, 5, 0}, {1, 10, 20}, {1, 1, 1}, 0, {}, true});
    const auto geometry = mj::game::build_geometry(level);
    mj::game::Simulation source(level, geometry.collision);
    std::vector<TickInput> inputs;
    std::vector<mj::game::Simulation> expected;
    bool long_jumped = false, wall_jumped = false;
    for (int i = 0; i < 600; ++i) {
        TickInput input{1, 0, 0, -0.32F, button::jump_held};
        if (i == 100)
            input.buttons |= button::jump_pressed | button::long_jump_held;
        if (!wall_jumped && source.player.current().wall_timer > 0)
            input.buttons |= button::jump_pressed;
        source.tick(input, k_dt);
        long_jumped = long_jumped || source.player.current().state == MoveState::long_jump;
        wall_jumped = wall_jumped || source.player.current().state == MoveState::wall_jump;
        inputs.push_back(input);
        expected.push_back(source);
    }
    REQUIRE(long_jumped);
    REQUIRE(wall_jumped);
    const auto parsed = mj::parse_replay(mj::serialize_replay(inputs));
    REQUIRE(parsed);
    for (const auto &cadence :
         std::vector<std::vector<double>>{{1.0 / 30},
                                          {1.0 / 60},
                                          {1.0 / 144},
                                          {1.0 / 240},
                                          {0.004, 0.019, 0.011, 0.031, 0.0007}}) {
        mj::game::Simulation replay(level, geometry.collision);
        mj::FixedStep step;
        for (std::size_t frame = 0; replay.ticks() < parsed->size(); ++frame) {
            step.advance(cadence[frame % cadence.size()], [&](float dt) {
                if (replay.ticks() >= parsed->size())
                    return;
                replay.tick((*parsed)[replay.ticks()], dt);
                identical(replay, expected[replay.ticks() - 1]);
            });
        }
    }
}

TEST_CASE("an early wall-jump press buffers until contact and holding never repeats it") {
    const auto world = wall_world(true);
    mj::game::Player player;
    player.place({1.5F, 20, 0}, mj::k_pi * 0.5F);
    for (int i = 0; i < 30 && player.current().position.x < 1.61F; ++i)
        player.tick({1, 0, 0, 0, 0}, world, k_dt);
    REQUIRE(player.current().wall_timer == 0.0F);
    player.tick({1, 0, 0, 0, button::jump_pressed | button::jump_held}, world, k_dt);
    bool kicked = false;
    for (int i = 0; i < 20; ++i) {
        player.tick({1, 0, 0, 0, button::jump_held}, world, k_dt);
        if (player.current().state == MoveState::wall_jump) {
            kicked = true;
            break;
        }
    }
    REQUIRE(kicked);
    for (int i = 0; i < 400; ++i)
        player.tick({1, 0, 0, 0, button::jump_held}, world, k_dt);
    CHECK(player.current().velocity.y < 0.0F);
    CHECK(player.current().state == MoveState::fall);
}

TEST_CASE("committed playground route runs, long jumps, wall climbs and activates its finish") {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);
    const auto replay =
        mj::load_replay(std::filesystem::path(MJ_TEST_REPLAYS) / "m4-moves-tour.mjr");
    REQUIRE(replay);
    mj::game::Simulation simulation(*level, geometry.collision);
    bool long_jumped = false;
    int kicks = 0;
    Vec3 last_kick;
    for (const auto &input : *replay) {
        simulation.tick(input, k_dt);
        const auto &p = simulation.player.current();
        long_jumped = long_jumped || p.state == MoveState::long_jump;
        if (p.state == MoveState::wall_jump && !same(p.last_kick_normal, last_kick)) {
            ++kicks;
            last_kick = p.last_kick_normal;
        }
    }
    CHECK(long_jumped);
    CHECK(kicks == 3);
    CHECK(simulation.ticks() == 2808);
    CHECK(simulation.active_checkpoint() == 10);
    CHECK(simulation.respawns() == 0);
    const auto &p = simulation.player.current();
    CHECK(p.grounded);
    CHECK_FALSE(p.wall_used);
    CHECK(p.position.x == doctest::Approx(64.470360F).epsilon(1e-4));
    CHECK(p.position.y == doctest::Approx(6.065001F).epsilon(1e-4));
    CHECK(p.position.z == doctest::Approx(-57.991585F).epsilon(1e-4));
    simulation.tick({0, 0, 0, 0, button::reset_pressed}, k_dt);
    CHECK(simulation.respawns() == 1);
    CHECK(mj::length(simulation.player.current().position - level->checkpoints[10].position) <
          1e-5F);
}

TEST_CASE("the taller authored wall pair has a reachable eight-meter exit") {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);
    mj::game::Simulation simulation(*level, geometry.collision);
    // Focused collision fixture; the committed replay covers the continuous spawn-to-first-pair
    // route.
    simulation.player.place({80, 0.01F, -53.3F}, 0);
    auto tick = [&](float x, float y, std::uint32_t buttons) {
        simulation.tick({x, y, 0, -0.32F, buttons}, k_dt);
    };
    for (int i = 0; i < 30; ++i)
        tick(0, 0, 0);
    tick(1, 0, button::jump_pressed | button::jump_held);
    float direction = 1;
    bool landed = false;
    int kicks = 0;
    for (int i = 0; i < 600; ++i) {
        const auto p = simulation.player.current();
        const bool edge = p.wall_timer > 0 &&
                          (!p.wall_used || mj::dot(p.wall_normal, p.last_kick_normal) <= 0.5F);
        if (p.position.y > 7.6F) {
            const auto v = mj::normalized(mj::horizontal(Vec3{80, 0, -56} - p.position));
            tick(v.x, -v.z, button::jump_held);
        } else {
            if (edge)
                direction = p.wall_normal.x;
            tick(direction, 0, button::jump_held | (edge ? button::jump_pressed : 0U));
        }
        if (edge)
            ++kicks;
        if (simulation.player.current().grounded && simulation.player.current().position.y > 7.9F) {
            landed = true;
            break;
        }
    }
    CHECK(landed);
    CHECK(kicks == 3);
    CHECK(simulation.respawns() == 0);
}
