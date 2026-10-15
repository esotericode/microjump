#include "test_framework.hpp"

#include <level.hpp>
#include <simulation.hpp>

#include <mj/core/fixed_step.hpp>

#include <bit>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

using mj::TickInput;
using mj::Vec3;
namespace button = mj::input_button;

namespace {
constexpr float k_dt = 1.0F / 120;

TickInput stick(float x, float y, float yaw = 0) {
    return {x, y, yaw, -0.32F, 0};
}
TickInput with(TickInput input, std::uint32_t buttons) {
    input.buttons |= buttons;
    return input;
}
mj::CollisionWorld world_of(std::initializer_list<mj::Box> boxes) {
    mj::SceneMesh mesh;
    for (const auto &box : boxes)
        mesh.add_box(box);
    return mj::CollisionWorld(mesh.triangles());
}
const mj::Box k_floor{{0, -0.5F, 0}, {200, 1, 200}, {1, 1, 1}};

mj::game::Player settled_player(const mj::CollisionWorld &world, Vec3 at, float facing = 0) {
    mj::game::Player player;
    player.place(at, facing);
    for (int i = 0; i < 10; ++i)
        player.tick({}, world, k_dt);
    return player;
}

float apex_of(const std::function<TickInput(int)> &script) {
    const auto world = world_of({k_floor});
    auto player = settled_player(world, {0, 0.005F, 0});
    const float start = player.current().position.y;
    float apex = start;
    for (int tick = 0; tick < 240; ++tick) {
        player.tick(script(tick), world, k_dt);
        apex = std::max(apex, player.current().position.y);
    }
    CHECK(player.current().grounded);
    CHECK(player.current().position.y == doctest::Approx(start).epsilon(0.01));
    return apex - start;
}

// Bitwise comparison: the determinism requirement is exact, not approximate.
bool identical(const mj::game::Simulation &a, const mj::game::Simulation &b) {
    auto same = [](Vec3 x, Vec3 y) {
        return std::bit_cast<std::uint32_t>(x.x) == std::bit_cast<std::uint32_t>(y.x) &&
               std::bit_cast<std::uint32_t>(x.y) == std::bit_cast<std::uint32_t>(y.y) &&
               std::bit_cast<std::uint32_t>(x.z) == std::bit_cast<std::uint32_t>(y.z);
    };
    const auto &p = a.player.current();
    const auto &q = b.player.current();
    return a.ticks() == b.ticks() && same(p.position, q.position) && same(p.velocity, q.velocity) &&
           std::bit_cast<std::uint32_t>(p.facing) == std::bit_cast<std::uint32_t>(q.facing) &&
           p.state == q.state && same(a.camera.focus(), b.camera.focus()) &&
           std::bit_cast<std::uint32_t>(a.camera.distance()) ==
               std::bit_cast<std::uint32_t>(b.camera.distance());
}

struct Yard {
    mj::game::Level level = mj::game::make_movement_yard();
    mj::game::LevelGeometry geometry = mj::game::build_geometry(level);
};
const Yard &yard() {
    static const Yard instance;
    return instance;
}

// Places the player in the yard, holds `input` and returns the final state.
mj::game::PlayerState hold_in_yard(Vec3 start, TickInput input, int ticks) {
    mj::game::Simulation simulation(yard().level, yard().geometry.collision);
    simulation.player.place(start, input.camera_yaw);
    for (int i = 0; i < ticks; ++i)
        simulation.tick(input, k_dt);
    return simulation.player.current();
}
} // namespace

TEST_CASE("held jump reaches the tuned apex; releasing early gives a lower jump") {
    const mj::game::PlayerTuning tuning;
    const float held = apex_of([](int tick) {
        return tick == 0 ? with({}, button::jump_pressed | button::jump_held)
                         : with({}, button::jump_held);
    });
    CHECK(held == doctest::Approx(tuning.jump_height).epsilon(0.04));
    const float tapped = apex_of([](int tick) {
        return tick == 0 ? with({}, button::jump_pressed | button::jump_held) : TickInput{};
    });
    CHECK(tapped > 0.5F);
    CHECK(tapped < held * 0.6F);
}

TEST_CASE("running accelerates to run speed, turns at a limited rate and skids on reversal") {
    const auto world = world_of({k_floor});
    auto player = settled_player(world, {0, 0.005F, 0});
    const mj::game::PlayerTuning tuning;
    int reached = -1;
    for (int tick = 0; tick < 120; ++tick) {
        player.tick(stick(0, 1), world, k_dt);
        const float speed = player.current().ground_speed;
        REQUIRE(speed <= tuning.run_speed + 1.0e-4F);
        if (reached < 0 && speed >= tuning.run_speed * 0.95F)
            reached = tick;
    }
    CHECK(reached > 0);
    CHECK(static_cast<float>(reached) * k_dt < 0.6F);
    CHECK(player.current().state == mj::game::MoveState::run);
    CHECK(player.current().velocity.z == doctest::Approx(-tuning.run_speed));

    // A 90 degree turn at speed takes several ticks rather than snapping.
    auto turning = player;
    turning.tick(stick(1, 0), world, k_dt);
    const float turned = std::abs(mj::angle_difference(0, turning.current().facing));
    CHECK(turned == doctest::Approx(tuning.turn_rate_fast * k_dt).epsilon(0.05));

    player.tick(stick(0, -1), world, k_dt);
    CHECK(player.current().state == mj::game::MoveState::skid);
    for (int tick = 0; tick < 60; ++tick)
        player.tick(stick(0, -1), world, k_dt);
    CHECK(player.current().state == mj::game::MoveState::run);
    CHECK(std::abs(mj::angle_difference(player.current().facing, mj::k_pi)) < 0.01F);
}

TEST_CASE("disabling jump grace periods still allows an immediate grounded jump") {
    const auto world = world_of({k_floor});
    for (const bool disable_coyote : {false, true}) {
        for (const bool disable_buffer : {false, true}) {
            auto player = settled_player(world, {0, 0.005F, 0});
            if (disable_coyote)
                player.tuning.coyote_time = 0;
            if (disable_buffer)
                player.tuning.jump_buffer = 0;
            player.tick(with({}, button::jump_pressed | button::jump_held), world, k_dt);
            CHECK_FALSE(player.current().grounded);
            CHECK(player.current().state == mj::game::MoveState::jump);
            CHECK(player.current().velocity.y > 0.0F);
        }
    }
}

TEST_CASE("coyote time allows a late jump off a ledge; buffered presses fire on landing") {
    // A 1 m high platform over a floor, running off its +X edge.
    const auto world = world_of({k_floor, {{0, 0.5F, 0}, {6, 1, 6}, {1, 1, 1}}});
    const mj::game::PlayerTuning tuning;
    auto run_off = [&](int late_ticks) {
        auto player = settled_player(world, {1.5F, 1.005F, 0}, mj::k_pi * 0.5F);
        const auto right = stick(0, 1, mj::k_pi * 0.5F);
        while (player.current().grounded)
            player.tick(right, world, k_dt);
        for (int i = 1; i < late_ticks; ++i)
            player.tick(right, world, k_dt);
        player.tick(with(right, button::jump_pressed | button::jump_held), world, k_dt);
        return player.current();
    };
    const int coyote_ticks = static_cast<int>(tuning.coyote_time / k_dt);
    CHECK(run_off(1).state == mj::game::MoveState::jump);
    CHECK(run_off(coyote_ticks - 1).state == mj::game::MoveState::jump);
    CHECK(run_off(coyote_ticks + 3).state == mj::game::MoveState::fall);

    auto drop_and_press = [&](float seconds_before_landing) {
        // Fall from 1.5 m; find the landing tick, then replay with an early press.
        auto probe = settled_player(world_of({k_floor}), {0, 0.005F, 0});
        probe.place({10, 1.5F, 10}, 0);
        const auto floor = world_of({k_floor});
        int landing = 0;
        while (!probe.current().grounded) {
            probe.tick({}, floor, k_dt);
            ++landing;
        }
        auto player = probe;
        player.place({10, 1.5F, 10}, 0);
        const int press = landing - static_cast<int>(seconds_before_landing / k_dt);
        for (int tick = 0; tick < landing + 2; ++tick)
            player.tick(tick == press ? with({}, button::jump_pressed | button::jump_held)
                                      : with({}, button::jump_held),
                        floor, k_dt);
        return player.current();
    };
    CHECK(drop_and_press(0.08F).state == mj::game::MoveState::jump);
    CHECK(drop_and_press(0.3F).grounded);
}

TEST_CASE("simulation is bit-identical at any render cadence and through a replay round trip") {
    // Live input is sampled per rendered frame and press edges are latched until a tick,
    // exactly like the game loop. Holding forward with a jump at the start hits the plaza
    // centerpiece, so collision, landing and camera smoothing all participate.
    const std::vector<std::vector<double>> cadences{
        {1.0 / 30}, {1.0 / 60}, {1.0 / 144}, {1.0 / 240}, {0.004, 0.019, 0.011, 0.031, 0.0007}};
    constexpr std::uint64_t k_ticks = 360;
    std::vector<TickInput> recorded;
    std::optional<mj::game::Simulation> reference;
    for (const auto &pattern : cadences) {
        CAPTURE(pattern.front());
        mj::game::Simulation simulation(yard().level, yard().geometry.collision);
        mj::FixedStep step;
        std::uint32_t pending = button::jump_pressed;
        std::vector<TickInput> ticks;
        for (std::size_t frame = 0; simulation.ticks() < k_ticks; ++frame) {
            const TickInput live = with(stick(0.2F, 0.97F, 0.3F), button::jump_held);
            step.advance(pattern[frame % pattern.size()], [&](float dt) {
                if (simulation.ticks() >= k_ticks)
                    return;
                TickInput tick = live;
                tick.buttons |= pending;
                pending = 0;
                ticks.push_back(tick);
                simulation.tick(tick, dt);
            });
        }
        if (!reference) {
            reference.emplace(simulation);
            recorded = ticks;
        } else {
            CHECK(identical(*reference, simulation));
        }
    }
    CHECK(reference->player.current().position.z < 6.0F); // It actually went somewhere.

    const auto parsed = mj::parse_replay(mj::serialize_replay(recorded));
    REQUIRE(parsed);
    REQUIRE(parsed->size() == recorded.size());
    mj::game::Simulation replayed(yard().level, yard().geometry.collision);
    for (const auto &tick : *parsed)
        replayed.tick(tick, k_dt);
    CHECK(identical(*reference, replayed));
}

TEST_CASE("yard collision: stairs and walkable ramps climb; steep ramp and tall step block") {
    using mj::game::MoveState;
    const float north = 0;              // Forward is -Z.
    const float south = mj::k_pi;       // Forward is +Z.
    const float east = mj::k_pi * 0.5F; // Forward is +X.

    const auto stairs = hold_in_yard({16, 0.005F, 2}, stick(0, 0.3F, south), 300);
    CHECK(stairs.grounded);
    CHECK(stairs.position.y == doctest::Approx(2.5F).epsilon(0.01));

    const auto ramp45 = hold_in_yard({25, 0.005F, -6}, stick(0, 0.4F, north), 240);
    CHECK(ramp45.grounded);
    CHECK(ramp45.position.y == doctest::Approx(3).epsilon(0.01));

    const auto ramp58 = hold_in_yard({30, 0.005F, -6}, stick(0, 0.4F, north), 240);
    CHECK(ramp58.position.y < 0.5F);
    CHECK(ramp58.position.z > -10.2F);

    const auto low_step = hold_in_yard({23, 0.005F, 7.5F}, stick(0, 0.5F, east), 120);
    CHECK(low_step.grounded);
    CHECK(low_step.position.y == doctest::Approx(0.3F).epsilon(0.03));

    const auto tall_step = hold_in_yard({23, 0.005F, 14.5F}, stick(0, 0.5F, east), 120);
    CHECK(tall_step.position.y < 0.05F);
    CHECK(tall_step.position.x < 27 - 1.2F - 0.35F + 0.02F);

    // Jumping under the 2.0 m tunnel roof bumps the head and falls straight back.
    mj::game::Simulation tunnel(yard().level, yard().geometry.collision);
    tunnel.player.place({6.5F, 0.005F, 15}, 0);
    float highest = 0;
    for (int i = 0; i < 120; ++i) {
        tunnel.tick(with({}, i == 2 ? button::jump_pressed | button::jump_held : button::jump_held),
                    k_dt);
        highest = std::max(highest, tunnel.player.current().position.y);
    }
    CHECK(highest < 2.0F - 1.6F + 0.02F);
    CHECK(highest > 0.3F);
    CHECK(tunnel.player.current().grounded);
}

TEST_CASE("falling out of the yard respawns at the active checkpoint; reset respawns too") {
    mj::game::Simulation simulation(yard().level, yard().geometry.collision);
    const auto spawn = yard().level.checkpoints.front().position;
    simulation.player.place({49, 0.005F, 8}, mj::k_pi * 0.5F);
    for (int i = 0; i < 400 && simulation.respawns() == 0; ++i)
        simulation.tick(stick(0, 1, mj::k_pi * 0.5F), k_dt);
    REQUIRE(simulation.respawns() == 1);
    CHECK(mj::length(simulation.player.current().position - spawn) < 1.0e-5F);
    simulation.tick(with({}, button::reset_pressed), k_dt);
    CHECK(simulation.respawns() == 2);
}

TEST_CASE("follow camera pulls in past walls and clamps its orientation") {
    mj::game::Level open;
    open.checkpoints.push_back({{0, 0.005F, 0}, 0});
    const auto empty = world_of({k_floor});
    mj::game::Simulation clear(open, empty);
    for (int i = 0; i < 30; ++i)
        clear.tick(stick(0, 0), k_dt);
    CHECK(clear.camera.distance() == doctest::Approx(clear.camera.tuning.distance).epsilon(0.01));

    // Yaw 0 looks toward -Z, so the camera sits at +Z: put a wall there.
    const auto walled = world_of({k_floor, {{0, 2, 3}, {10, 4, 1}, {1, 1, 1}}});
    mj::game::Simulation blocked(open, walled);
    for (int i = 0; i < 30; ++i)
        blocked.tick(stick(0, 0), k_dt);
    const auto pose = blocked.camera.pose(1, walled);
    CHECK(pose.position.z < 2.5F - blocked.camera.tuning.probe_radius + 0.01F);
    CHECK(blocked.camera.distance() < blocked.camera.tuning.distance * 0.5F);

    blocked.camera.set_orientation({0, -5});
    CHECK(blocked.camera.pitch() == doctest::Approx(blocked.camera.tuning.min_pitch));
    blocked.camera.set_orientation({7, 5});
    CHECK(blocked.camera.pitch() == doctest::Approx(blocked.camera.tuning.max_pitch));
    CHECK(blocked.camera.yaw() == doctest::Approx(7 - mj::k_tau));
}

TEST_CASE("camera collision overrides its preferred minimum distance at close walls") {
    mj::game::PlayerState player;
    player.position = {0, 0.005F, 0};
    // The wall leaves room for the player but less than the camera's preferred 0.9 m.
    const auto world = world_of({k_floor, {{0, 2, 1}, {10, 4, 1}, {1, 1, 1}}});
    mj::game::FollowCamera camera;
    camera.snap(player, 0);
    camera.tick(player, world, k_dt);
    for (const float alpha : {0.0F, 0.5F, 1.0F}) {
        const auto pose = camera.pose(alpha, world);
        CHECK(pose.position.z + camera.tuning.probe_radius < 0.5F);
    }
}

TEST_CASE("committed replay of a real input session reproduces its recorded outcome") {
    // Recorded with tools/input-smoke.sh (real X11 keyboard/mouse, software Vulkan); the game
    // logged this final state when recording. Same-build replays are bit-identical; the
    // tolerance only absorbs float differences between compilers and C libraries.
    const auto ticks =
        mj::load_replay(std::filesystem::path(MJ_TEST_REPLAYS) / "m2-input-smoke.mjr");
    REQUIRE(ticks);
    REQUIRE(ticks->size() == 439);
    mj::game::Simulation simulation(yard().level, yard().geometry.collision);
    bool jumped = false;
    for (const auto &tick : *ticks) {
        simulation.tick(tick, k_dt);
        jumped = jumped || simulation.player.current().state == mj::game::MoveState::jump;
    }
    const auto &state = simulation.player.current();
    CHECK(jumped);
    CHECK(state.grounded);
    CHECK(state.state == mj::game::MoveState::run);
    CHECK(state.position.x == doctest::Approx(8.37801552F).epsilon(1.0e-4));
    CHECK(state.position.y == doctest::Approx(0.00595231727F).epsilon(1.0e-2));
    CHECK(state.position.z == doctest::Approx(-12.5603285F).epsilon(1.0e-4));
    CHECK(simulation.respawns() == 0);
}
