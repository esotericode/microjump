#include "test_framework.hpp"

#include <level_io.hpp>
#include <player.hpp>
#include <simulation.hpp>

#include <mj/scene/mesh.hpp>

#include <filesystem>

namespace {
namespace button = mj::input_button;
constexpr float k_dt = 1.0F / 120;
using mj::game::HopTiming;
using mj::game::MoveState;
using mj::game::Player;

constexpr std::uint32_t k_held = button::jump_held;
constexpr std::uint32_t k_press = button::jump_pressed | button::jump_held;

const mj::CollisionWorld &flat_world() {
    static const mj::CollisionWorld world = [] {
        mj::SceneMesh mesh;
        mesh.add_box({{0, -0.5F, 0}, {2000, 1, 2000}, {1, 1, 1}});
        return mj::CollisionWorld(mesh.triangles());
    }();
    return world;
}

// Forward is -Z with camera yaw 0.
mj::TickInput forward(std::uint32_t buttons = 0, std::uint32_t modifier = 0) {
    return {0, 1, 0, 0, buttons | modifier};
}
float flat_speed(const Player &player) {
    return mj::length(mj::horizontal(player.current().velocity));
}

Player running(const mj::CollisionWorld &world = flat_world()) {
    Player player;
    player.place({0, 0.01F, 900}, 0);
    for (int i = 0; i < 240; ++i)
        player.tick(forward(), world, k_dt);
    return player;
}

// Ticks until the player lands while holding forward and jump.
int ticks_to_land(Player player, std::uint32_t modifier = 0) {
    for (int i = 1; i < 600; ++i) {
        player.tick(forward(k_held, modifier), flat_world(), k_dt);
        if (player.current().grounded)
            return i;
    }
    return -1;
}

enum class Press : std::uint8_t { clean, early, mash, late };

// From an airborne player, presses jump as described and returns once the next grounded
// takeoff has happened (the player is airborne again).
void hop(Player &player, Press press, std::uint32_t modifier = 0) {
    const int landing = ticks_to_land(player, modifier);
    REQUIRE(landing > 20);
    for (int i = 1; i <= landing; ++i) {
        const int before = landing - i; // Ticks left until the landing tick.
        bool pressed = false;
        if (press == Press::clean)
            pressed = before == 3;
        else if (press == Press::early)
            pressed = before == 12; // 0.1 s: inside the buffer, outside the hop window.
        else if (press == Press::mash)
            pressed = before <= 40 && before % 4 == 0; // Includes a press inside the window.
        player.tick(forward(pressed ? k_press : k_held, modifier), flat_world(), k_dt);
    }
    REQUIRE(player.current().grounded);
    if (press == Press::late) {
        for (int i = 0; i < 24; ++i) // 0.2 s on the ground.
            player.tick(forward(k_held, modifier), flat_world(), k_dt);
        player.tick(forward(k_press, modifier), flat_world(), k_dt);
    } else {
        player.tick(forward(k_held, modifier), flat_world(), k_dt); // Buffered takeoff.
    }
    REQUIRE_FALSE(player.current().grounded);
}

Player long_jumping() {
    auto player = running();
    player.tick(forward(k_press, button::long_jump_held), flat_world(), k_dt);
    REQUIRE(player.current().state == MoveState::long_jump);
    return player;
}
} // namespace

TEST_CASE("clean hops carry momentum; early, mashed and late presses lose it") {
    const float run = Player{}.tuning.run_speed;
    const float launched = flat_speed(long_jumping());
    CHECK(launched == doctest::Approx(run * Player{}.tuning.long_jump_speed_scale).epsilon(1e-3));

    auto clean = long_jumping();
    for (int i = 0; i < 3; ++i) {
        hop(clean, Press::clean);
        CHECK(clean.current().last_hop == HopTiming::clean);
    }
    // Only air drag is lost: about a quarter m/s per hop.
    CHECK(flat_speed(clean) > launched - 1.5F);
    CHECK(flat_speed(clean) < launched);

    auto early = long_jumping();
    hop(early, Press::early);
    CHECK(early.current().last_hop == HopTiming::early);
    auto mash = long_jumping();
    hop(mash, Press::mash);
    CHECK(mash.current().last_hop == HopTiming::early); // Mashing cannot find the window.
    auto late = long_jumping();
    hop(late, Press::late);
    CHECK(late.current().last_hop == HopTiming::late);
    for (const Player *sloppy : {&early, &mash}) {
        const float kept = (flat_speed(*sloppy) - run) / (launched - run);
        CHECK(kept == doctest::Approx(Player{}.tuning.early_hop_retention).epsilon(0.08));
    }
    CHECK(flat_speed(late) < launched - 2.0F); // Ground friction after the window.
    CHECK(flat_speed(late) > run);

    // Repeated sloppy hops return to run speed; clean hops keep most of the speed.
    auto careless = long_jumping();
    for (int i = 0; i < 4; ++i)
        hop(careless, Press::early);
    CHECK(flat_speed(careless) < run + 0.3F);
}

TEST_CASE("clean long-jump chains build speed up to the cap; sloppy chains do not") {
    const auto tuning = Player{}.tuning;
    auto clean = long_jumping();
    float previous = flat_speed(clean);
    for (int i = 0; i < 6; ++i) {
        hop(clean, Press::clean, button::long_jump_held);
        CHECK(clean.current().state == MoveState::long_jump);
        CHECK(flat_speed(clean) >= previous - 1e-3F);
        CHECK(flat_speed(clean) <= tuning.max_speed + 1e-4F);
        previous = flat_speed(clean);
    }
    CHECK(previous == doctest::Approx(tuning.max_speed).epsilon(1e-3));

    auto sloppy = long_jumping();
    for (int i = 0; i < 4; ++i)
        hop(sloppy, Press::early, button::long_jump_held);
    CHECK(flat_speed(sloppy) ==
          doctest::Approx(tuning.run_speed * tuning.long_jump_speed_scale).epsilon(0.01));
}

TEST_CASE("momentum buys distance, not height") {
    auto apex_from = [](Player player) {
        const float start = player.current().position.y;
        player.tick(forward(k_press), flat_world(), k_dt);
        float apex = 0;
        for (int i = 0; i < 240 && !player.current().grounded; ++i) {
            player.tick(forward(k_held), flat_world(), k_dt);
            apex = std::max(apex, player.current().position.y - start);
        }
        return apex;
    };
    // Land from a long jump and jump cleanly, versus an ordinary running jump.
    auto fast = long_jumping();
    const int landing = ticks_to_land(fast, button::long_jump_held);
    for (int i = 0; i < landing; ++i)
        fast.tick(forward(k_held), flat_world(), k_dt);
    REQUIRE(fast.current().ground_speed > fast.tuning.run_speed + 5);
    CHECK(apex_from(fast) == doctest::Approx(apex_from(running())).epsilon(0.01));
}

TEST_CASE("long jumps and wall kicks keep fixed arcs; ordinary jumps stay variable") {
    auto apex = [](bool hold_after_takeoff, std::uint32_t modifier) {
        auto player = running();
        const float start = player.current().position.y;
        player.tick(forward(k_press, modifier), flat_world(), k_dt);
        float top = 0;
        for (int i = 0; i < 240 && !player.current().grounded; ++i) {
            player.tick(forward(hold_after_takeoff ? k_held : 0U, modifier), flat_world(), k_dt);
            top = std::max(top, player.current().position.y - start);
        }
        return top;
    };
    CHECK(apex(false, button::long_jump_held) ==
          doctest::Approx(apex(true, button::long_jump_held)));
    CHECK(apex(false, 0) < apex(true, 0) * 0.7F);

    mj::SceneMesh mesh;
    mesh.add_box({{2.5F, 0, 0}, {1, 100, 20}, {1, 1, 1}});
    auto triangles = mesh.triangles();
    for (auto &triangle : triangles)
        triangle.wall_jump = true;
    const mj::CollisionWorld wall(triangles);
    auto kick = [&](bool hold) {
        Player player;
        player.place({1.5F, 20, 0}, mj::k_pi * 0.5F);
        for (int i = 0; i < 25; ++i)
            player.tick({1, 0, 0, 0, 0}, wall, k_dt);
        player.tick({1, 0, 0, 0, k_press}, wall, k_dt);
        REQUIRE(player.current().state == MoveState::wall_jump);
        const float start = player.current().position.y;
        float top = start;
        for (int i = 0; i < 120; ++i) {
            player.tick({0, 0, 0, 0, hold ? k_held : 0U}, wall, k_dt);
            top = std::max(top, player.current().position.y);
        }
        return top - start;
    };
    CHECK(kick(false) == doctest::Approx(kick(true)));
    CHECK(kick(false) > 2.0F);
}

TEST_CASE("downhill running builds momentum; uphill and flat ground drain it") {
    // A 20 degree, 30 m slope whose high edge faces -Z, on a large floor.
    const float length = 30;
    const float height = length * std::tan(0.349F);
    mj::SceneMesh mesh;
    mesh.add_box({{0, -0.5F, 0}, {400, 1, 400}, {1, 1, 1}});
    mesh.add_ramp({{0, height * 0.5F, 0}, {8, height, length}, {1, 1, 1}});
    const mj::CollisionWorld world(mesh.triangles());
    const float run = Player{}.tuning.run_speed;

    // Downhill: start on the top edge and run toward +Z (camera yaw pi).
    Player down;
    down.place({0, height + 0.05F, -length * 0.5F + 0.6F}, mj::k_pi);
    float fastest = 0;
    for (int i = 0; i < 600 && down.current().position.z < length * 0.5F; ++i) {
        down.tick({0, 1, mj::k_pi, 0, 0}, world, k_dt);
        fastest = std::max(fastest, down.current().ground_speed);
    }
    CHECK(fastest > run + 2);
    CHECK(fastest <= Player{}.tuning.max_speed);
    // On the flat beyond the slope the extra speed wears off within about a second.
    for (int i = 0; i < 150; ++i)
        down.tick({0, 1, mj::k_pi, 0, 0}, world, k_dt);
    CHECK(down.current().ground_speed == doctest::Approx(run));

    // Standing still on the slope does not slide, and releasing the stick brakes normally.
    Player idle;
    idle.place({0, height * 0.5F + 0.05F, 0}, 0);
    for (int i = 0; i < 240; ++i)
        idle.tick({}, world, k_dt);
    CHECK(idle.current().ground_speed == 0.0F);

    // Uphill drains momentum faster than flat ground: land a long jump on each.
    auto drain_after_landing = [](const mj::CollisionWorld &surface) {
        Player player = running(surface);
        player.tick(forward(k_press, button::long_jump_held), surface, k_dt);
        for (int i = 0; i < 600 && !player.current().grounded; ++i)
            player.tick(forward(k_held), surface, k_dt);
        const float landed = player.current().ground_speed;
        for (int i = 0; i < 30; ++i)
            player.tick(forward(), surface, k_dt);
        return landed - player.current().ground_speed;
    };
    Player probe = running();
    probe.tick(forward(k_press, button::long_jump_held), flat_world(), k_dt);
    for (int i = 0; i < 600 && !probe.current().grounded; ++i)
        probe.tick(forward(k_held), flat_world(), k_dt);
    const float landing_z = probe.current().position.z;
    mj::SceneMesh hill;
    hill.add_box({{0, -0.5F, 900}, {400, 1, 400}, {1, 1, 1}});
    // Low edge just behind the landing point; the slope rises in the direction of travel.
    hill.add_ramp(
        {{0, height * 0.5F, landing_z + 1 - length * 0.5F}, {8, height, length}, {1, 1, 1}});
    const mj::CollisionWorld uphill(hill.triangles());
    const float flat_loss = drain_after_landing(flat_world());
    const float uphill_loss = drain_after_landing(uphill);
    CHECK(flat_loss > 1.5F);
    CHECK(uphill_loss > flat_loss + 0.5F);
}

TEST_CASE("stepping off a low pad does not count as a slope") {
    // Rounded contact at the pad edge tilts the contact normal; speed must match flat ground.
    mj::SceneMesh mesh;
    mesh.add_box({{0, -0.5F, 0}, {400, 1, 400}, {1, 1, 1}});
    mesh.add_box({{0, 0.03F, 0}, {4, 0.06F, 4}, {1, 1, 1}});
    const mj::CollisionWorld padded(mesh.triangles());
    Player off_pad, flat;
    off_pad.place({0, 0.07F, 0}, 0);
    flat.place({0, 0.01F, 0}, 0);
    for (int i = 0; i < 120; ++i) {
        off_pad.tick(forward(), padded, k_dt);
        flat.tick(forward(), flat_world(), k_dt);
        CHECK(off_pad.current().ground_speed == doctest::Approx(flat.current().ground_speed));
    }
}

TEST_CASE("committed sprint-lane chain builds momentum to the cap with clean takeoffs") {
    // Scripted (not a real input session): four long jumps chained 25 ms before touchdown.
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto geometry = mj::game::build_geometry(*level);
    const auto replay =
        mj::load_replay(std::filesystem::path(MJ_TEST_REPLAYS) / "m4-momentum-chain.mjr");
    REQUIRE(replay);
    mj::game::Simulation simulation(*level, geometry.collision);
    int clean = 0;
    float fastest = 0;
    for (const auto &input : *replay) {
        const bool was_grounded = simulation.player.current().grounded;
        simulation.tick(input, k_dt);
        const auto &p = simulation.player.current();
        if (was_grounded && !p.grounded && p.last_hop == HopTiming::clean)
            ++clean;
        CHECK(p.last_hop != HopTiming::early);
        fastest = std::max(fastest, flat_speed(simulation.player));
    }
    CHECK(clean == 4);
    CHECK(fastest == doctest::Approx(simulation.player.tuning.max_speed).epsilon(1e-3));
    CHECK(simulation.respawns() == 0);
    CHECK(simulation.player.current().state == MoveState::idle);
}
