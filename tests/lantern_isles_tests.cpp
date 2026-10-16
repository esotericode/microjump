#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

using mj::Vec3;

namespace {
constexpr float k_dt = 1.0F / 120;
void trace(const char *tag, const mj::game::Simulation &simulation) {
    if (std::getenv("ROUTELOG") == nullptr)
        return;
    const auto &s = simulation.player.current();
    std::printf("%s t%llu %.2f %.2f %.2f v %.2f %.2f %.2f %s %s r%u\n", tag,
                static_cast<unsigned long long>(simulation.ticks()), double(s.position.x),
                double(s.position.y), double(s.position.z), double(s.velocity.x),
                double(s.velocity.y), double(s.velocity.z), s.grounded ? "G" : "A",
                mj::game::to_string(s.state), simulation.respawns());
}

mj::game::Level load(std::string_view name) {
    auto level = mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / name);
    REQUIRE(level);
    return std::move(*level);
}

std::size_t spark_index(const mj::game::Level &level, std::string_view name) {
    for (std::size_t i = 0; i < level.collectibles.size(); ++i) {
        if (level.collectibles[i].name == name)
            return i;
    }
    FAIL("Missing authored spark: " << name);
    return level.collectibles.size();
}

// A camera-relative stick (camera yaw 0) pointing at `target`.
mj::TickInput toward(const mj::game::Simulation &simulation, Vec3 target, float strength = 1,
                     std::uint32_t buttons = mj::input_button::jump_held) {
    const Vec3 offset = mj::horizontal(target - simulation.player.current().position);
    const float distance = mj::length(offset);
    if (distance < 1e-3F)
        return {0, 0, 0, -0.32F, buttons};
    const float scale = strength * std::min(1.0F, distance / 0.6F) / distance;
    return {offset.x * scale, -offset.z * scale, 0, -0.32F, buttons};
}

// In the air, steer for the velocity that touches down on `top` just as the jump falls to its
// height, as a player lines up a landing instead of pushing at it.
mj::TickInput aim(const mj::game::Simulation &simulation, Vec3 top, std::uint32_t buttons) {
    const auto &state = simulation.player.current();
    const auto &tuning = simulation.player.tuning;
    const float gravity = tuning.gravity() * tuning.fall_gravity_scale;
    const float drop = state.position.y - top.y;
    const float reach = state.velocity.y * state.velocity.y + 2 * gravity * drop;
    if (reach <= 0)
        return toward(simulation, top, 1, buttons); // Still below it: just press on.
    const float time = std::max((state.velocity.y + std::sqrt(reach)) / gravity, 0.05F);
    const Vec3 wanted = mj::horizontal(top - state.position) * (1 / time);
    // Steering accelerates along the stick (and nothing slows you without it), so push by
    // the change still needed, reaching it within about a tenth of a second.
    const Vec3 change = wanted - mj::horizontal(state.velocity);
    const float needed = mj::length(change);
    if (needed < 1e-3F)
        return {0, 0, 0, -0.32F, buttons};
    const Vec3 stick =
        change * (std::min(1.0F, needed / (tuning.air_acceleration * 0.1F)) / needed);
    return {stick.x, -stick.z, 0, -0.32F, buttons};
}

// One hop of a route: the landing's top center and how close to its middle counts as landed.
struct Landing {
    Vec3 top;
    float radius = 1.2F;
    // Small tops (columns) want a gentle hop: settle below this speed before leaping.
    float max_speed = 100;
};

// Seconds from a held jump's takeoff until it falls back to `rise` above the takeoff.
float flight_time(const mj::game::PlayerTuning &tuning, float rise) {
    const float gravity = tuning.gravity();
    const float launch = tuning.jump_velocity();
    const float apex = launch * launch / (2 * gravity);
    const float fall = std::max(apex - rise, 0.0F);
    return launch / gravity + std::sqrt(2 * fall / (gravity * tuning.fall_gravity_scale));
}

// Plays each hop the way a player judges one: turn toward the landing, take off once the
// current speed would carry the jump there (or at the edge of the ground underfoot), then
// line up the touchdown in the air. Fails on any fall: every hop on a shipped route must be
// an ordinary jump.
void hop(mj::game::Simulation &simulation, const mj::CollisionWorld &world,
         std::initializer_list<Landing> landings) {
    int index = 0;
    for (const auto &landing : landings) {
        CAPTURE(index);
        bool arrived = false;
        for (int tick = 0; tick < 900 && !arrived; ++tick) {
            const auto &state = simulation.player.current();
            const Vec3 offset = mj::horizontal(landing.top - state.position);
            const float distance = mj::length(offset);
            // Ground all the way there (within a step) means walk; any gap means hop.
            bool connected = std::abs(state.position.y - landing.top.y) < 0.35F;
            for (float along = 0.4F; connected && along < distance; along += 0.4F) {
                const Vec3 probe = state.position + offset * (along / distance);
                const auto floor = world.raycast({probe + Vec3{0, 0.5F, 0}, {0, -1, 0}, 1.0F});
                connected = floor && std::abs(floor->point.y - state.position.y) < 0.35F;
            }
            const Vec3 ahead = state.position + offset * (0.5F / std::max(distance, 0.5F));
            const auto ground = world.raycast({ahead + Vec3{0, 0.5F, 0}, {0, -1, 0}, 1.0F});
            const bool edge = !ground || ground->point.y < state.position.y - 0.6F;
            const Vec3 moving = mj::horizontal(state.velocity);
            const float speed = mj::length(moving);
            const bool facing = speed < 1.0F || mj::dot(moving, offset) > 0.95F * speed * distance;
            const float carry =
                speed * flight_time(simulation.player.tuning, landing.top.y - state.position.y);
            std::uint32_t buttons = mj::input_button::jump_held;
            mj::TickInput input;
            if (!state.grounded) {
                input = aim(simulation, landing.top, buttons);
            } else if (connected) {
                input = toward(simulation, landing.top, 0.6F, buttons); // Walk there.
            } else if (speed > landing.max_speed) {
                input = {0, 0, 0, -0.32F, buttons}; // Settle before a careful hop.
            } else {
                if ((facing && carry >= distance - 0.4F) || edge)
                    buttons |= mj::input_button::jump_pressed;
                // Ease off while still turning, so the turn finishes before the edge.
                input = toward(simulation, landing.top, facing ? 1.0F : 0.4F, buttons);
            }
            simulation.tick(input, k_dt);
            const auto &after = simulation.player.current();
            arrived = after.grounded && std::abs(after.position.y - landing.top.y) < 0.3F &&
                      mj::length(mj::horizontal(landing.top - after.position)) < landing.radius;
            REQUIRE(simulation.respawns() == 0);
        }
        REQUIRE(arrived);
        ++index;
    }
}

// Walks to `target` without jumping, then stands still long enough to settle.
void walk(mj::game::Simulation &simulation, Vec3 target, float strength = 0.6F) {
    for (int tick = 0; tick < 600; ++tick) {
        if (mj::length(mj::horizontal(target - simulation.player.current().position)) < 0.3F)
            break;
        simulation.tick(toward(simulation, target, strength), k_dt);
    }
    for (int tick = 0; tick < 30; ++tick)
        simulation.tick({}, k_dt);
}
} // namespace

TEST_CASE("the Lantern Isles load as a floating, drawn-and-solid place") {
    const auto level = load("lantern-isles.json");
    CHECK(level.title == "The Lantern Isles");
    CHECK(level.atmosphere.sky_below);
    CHECK_FALSE(level.atmosphere.grid);
    CHECK(level.collectibles.size() == 16);
    const auto geometry = mj::game::build_geometry(level);
    // Clouds and far islands are scenery: nothing below the routes catches a fall.
    CHECK_FALSE(geometry.collision.raycast({{0, level.kill_height + 0.5F, 70}, {0, -1, 0}, 100}));
    for (std::size_t i = 0; i < level.checkpoints.size(); ++i) {
        CAPTURE(i);
        const auto &checkpoint = level.checkpoints[i];
        mj::game::Simulation simulation(level, geometry.collision, i);
        for (int tick = 0; tick < 90; ++tick)
            simulation.tick({}, k_dt);
        CHECK(simulation.player.current().grounded);
        CHECK(mj::length(simulation.player.current().position - checkpoint.position) < 0.1F);
        CHECK(simulation.respawns() == 0);
    }
}

TEST_CASE("every gate leads to a shipped level and checkpoint, and back again") {
    for (const std::string_view name : {"playground.json", "lantern-isles.json"}) {
        CAPTURE(name);
        const auto level = load(name);
        REQUIRE_FALSE(level.portals.empty());
        const auto geometry = mj::game::build_geometry(level);
        for (const auto &portal : level.portals) {
            CAPTURE(portal.name);
            const auto target = load(portal.level);
            REQUIRE(portal.checkpoint < target.checkpoints.size());
            CHECK(portal.position.y > level.kill_height);
            // The gate stands over walkable ground, so it can be walked into.
            const auto ground = geometry.collision.raycast({portal.position, {0, -1, 0}, 3});
            REQUIRE(ground);
            // You never arrive inside the way back.
            for (const auto &back : target.portals) {
                const Vec3 arrival = target.checkpoints[portal.checkpoint].position;
                CHECK(mj::length(arrival - back.position) > back.radius + 3);
            }
        }
    }
    // Walking into the isles' home gate reports it; the game, not the simulation, travels.
    const auto isles = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(isles);
    mj::game::Simulation simulation(isles, geometry.collision);
    bool entered = false;
    for (int tick = 0; tick < 400 && !entered; ++tick) {
        simulation.tick(toward(simulation, isles.portals.front().position, 0.6F), k_dt);
        entered = simulation.portal_entered().has_value();
    }
    CHECK(entered);
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("Lantern Isles sparks sit over walkable landings, except the one you leap for") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    const mj::game::PlayerTuning tuning;
    for (std::size_t i = 0; i < level.collectibles.size(); ++i) {
        const auto &spark = level.collectibles[i];
        CAPTURE(spark.name);
        if (spark.name == "Leap of faith" || spark.name == "Over the gap")
            continue; // Hang over gaps; the route tests collect them mid-leap.
        const auto support = geometry.collision.raycast({spark.position, {0, -1, 0}, 2});
        REQUIRE(support);
        CHECK(mj::is_walkable(support->normal, tuning.collision));
        CHECK(support->distance > 0.8F);
        CHECK(support->distance < 1.25F);
        mj::game::Simulation simulation(level, geometry.collision);
        simulation.player.place(support->point + Vec3{0, 0.02F, 0}, 0);
        for (int tick = 0; tick < 60; ++tick)
            simulation.tick({}, k_dt);
        CHECK(simulation.player.current().grounded);
        CHECK(mj::length(simulation.player.current().position - support->point) < 0.1F);
        CHECK(simulation.collectible_collected(i));
        CHECK(simulation.respawns() == 0);
    }
}

TEST_CASE("Lantern Isles routes can be crossed with ordinary jumps") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);

    SUBCASE("spawn, stepping stones and the grove's canopies up to the blossom crown") {
        mj::game::Simulation simulation(level, geometry.collision);
        walk(simulation, {3, 0, -3});
        CHECK(simulation.collectible_collected(spark_index(level, "Welcome lantern")) == false);
        hop(simulation, geometry.collision,
            {{{4, 1.2F, -4}, 0.6F}, // The welcome lantern's plinth.
             {{1, 0.5F, -14}},
             {{-1.5F, 1.0F, -19.2F}},
             {{1, 1.6F, -24.4F}},
             {{-1, 2.2F, -29.4F}},
             {{0, 3, -35.5F}, 1.0F}});
        CHECK(simulation.collectible_collected(spark_index(level, "Welcome lantern")));
        CHECK(simulation.collectible_collected(spark_index(level, "Stepping glow")));
        CHECK(simulation.active_checkpoint() == 1);
        // Bush, four canopies, each about 1.4 m higher than the last.
        hop(simulation, geometry.collision,
            {{{-5, 4.4F, -38.5F}, 0.8F},
             {{-9.5F, 5.8F, -41.5F}},
             {{-11, 7.2F, -46.5F}},
             {{-8, 8.6F, -51}},
             {{-3, 10, -52.5F}}});
        walk(simulation, {-3, 10, -52.5F}, 0.3F);
        CHECK(simulation.collectible_collected(spark_index(level, "Blossom crown")));
    }
    SUBCASE("slabs west to the Crystal Spire") {
        mj::game::Simulation simulation(level, geometry.collision);
        hop(simulation, geometry.collision,
            {{{-9, 0, -3}, 1.0F}, // Around the gate's dais.
             {{-16.5F, 0.4F, -2.5F}},
             {{-23, 0.7F, -4.5F}},
             {{-36, 1.0F, -6}, 1.0F}});
        CHECK(simulation.active_checkpoint() == 2);
    }
    SUBCASE("slabs east to the Old Ruins, then column by column to the arch") {
        mj::game::Simulation simulation(level, geometry.collision);
        hop(simulation, geometry.collision,
            {{{8, 0, -1}, 1.0F}, {{16.5F, 0.5F, -1}}, {{23, 1.0F, -1.5F}}, {{32, 1.5F, -2}, 1.0F}});
        CHECK(simulation.active_checkpoint() == 4);
        hop(simulation, geometry.collision,
            {{{35, 2.7F, -6.5F}, 0.6F, 6},
             {{37.2F, 3.9F, -8.6F}, 0.6F, 4},
             {{40.1F, 5.1F, -9.3F}, 0.6F, 4},
             {{42.8F, 6.3F, -8.0F}, 0.6F, 4},
             {{44.2F, 8.0F, -5.8F}, 0.7F, 4}}); // The lintel's near end.
        walk(simulation, {45, 8, -2.9F}, 0.3F);
        CHECK(simulation.collectible_collected(spark_index(level, "Column climb")));
        CHECK(simulation.collectible_collected(spark_index(level, "Ruin crown")));
    }
    SUBCASE("cloud steps from Lantern Isle back up to the pier, past the nook") {
        mj::game::Simulation simulation(level, geometry.collision, 5);
        hop(simulation, geometry.collision,
            {{{-11, -3.2F, 49}},
             {{-14, -2.4F, 43.5F}},
             {{-20, -2.0F, 40.5F}, 1.0F},
             {{-14.5F, -1.6F, 38}},
             {{-13, -0.8F, 32.5F}},
             {{-11, 0, 27.3F}},
             {{-6.5F, 0, 23}},
             {{0, 0, 22}, 1.5F}});
        CHECK(simulation.collectible_collected(spark_index(level, "Cloud nook")));
        CHECK(simulation.respawns() == 0);
    }
}

TEST_CASE("the long pier's gap takes a long jump, and a running jump falls short") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    const auto leap = [&](bool long_jump) {
        mj::game::Simulation simulation(level, geometry.collision);
        simulation.player.place({0, 0.01F, 12}, mj::k_pi);
        for (int tick = 0; tick < 600; ++tick) {
            const auto &state = simulation.player.current();
            std::uint32_t buttons = mj::input_button::jump_held;
            if (state.grounded && state.position.z > 34.2F && state.position.y > -0.5F) {
                buttons |= mj::input_button::jump_pressed;
                if (long_jump)
                    buttons |= mj::input_button::long_jump_held;
            }
            simulation.tick({0, -1, 0, -0.32F, buttons}, k_dt); // Run south (+Z).
            if (simulation.respawns() > 0 ||
                (state.grounded && state.position.z > 46 && state.position.y < -3.5F))
                break;
        }
        return simulation;
    };
    const auto leapt = leap(true);
    CHECK(leapt.respawns() == 0);
    CHECK(leapt.player.current().position.y == doctest::Approx(-4).epsilon(0.02));
    CHECK(leapt.collectible_collected(spark_index(level, "Leap of faith")));
    const auto fell = leap(false);
    CHECK(fell.respawns() == 1);
    CHECK(fell.active_checkpoint() == 0);
}

TEST_CASE("the Crystal Spire climbs by alternating wall kicks onto its lookout tower") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    mj::game::Simulation simulation(level, geometry.collision);
    // The two kick walls' inner faces, and the tower against the west wall's outer face.
    constexpr float k_west = -41.6F, k_east = -38.4F, k_deck_top = 9.0F;
    const Vec3 lookout{-44.4F, k_deck_top, -18};
    simulation.player.place({-40, 1.01F, -16}, 0);
    float side = 1; // Kick off the east wall first.
    int kicks = 0;
    bool topped = false;
    for (int tick = 0; tick < 1200; ++tick) {
        const auto &state = simulation.player.current();
        std::uint32_t buttons = mj::input_button::jump_held;
        mj::TickInput input{side, 0, 0, -0.32F, 0};
        if (state.position.y > k_deck_top - 0.6F) {
            topped = true;
        }
        if (topped) {
            input = toward(simulation, lookout);
        } else if (state.grounded) {
            buttons |= mj::input_button::jump_pressed;
        } else {
            const float wall = side > 0 ? k_east : k_west;
            if (std::abs(state.position.x - wall) < 0.5F && state.velocity.y < 6) {
                buttons |= mj::input_button::jump_pressed;
            }
        }
        input.buttons = buttons;
        const float rising = state.velocity.y;
        const bool airborne = !state.grounded;
        simulation.tick(input, k_dt);
        const auto &after = simulation.player.current();
        // A kick relaunches upward at a fixed speed, even straight out of another kick.
        if (airborne && after.state == mj::game::MoveState::wall_jump &&
            after.velocity.y > rising + 5) {
            ++kicks;
            side = -side;
        }
        if (topped && after.grounded && after.position.y > k_deck_top - 0.2F)
            break;
        REQUIRE(simulation.respawns() == 0);
    }
    CHECK(kicks >= 3);
    CHECK(simulation.player.current().grounded);
    CHECK(simulation.player.current().position.y == doctest::Approx(k_deck_top).epsilon(0.02));
    walk(simulation, {-44.4F, k_deck_top, -20}, 0.4F);
    CHECK(simulation.collectible_collected(spark_index(level, "Crystal lookout")));
    walk(simulation, {-44.4F, k_deck_top, -15.8F}, 0.4F);
    CHECK(simulation.active_checkpoint() == 3);
    CHECK(simulation.respawns() == 0);
}

namespace {
// Runs from `start` toward `top` (a ledge's top center) and jumps `stages` times in a row,
// each the moment it lands, the first `first` metres from the ledge. Lines up the final
// leap in the air. Returns whether it ends standing on the ledge.
bool chain_onto(const mj::game::Level &level, const mj::CollisionWorld &world, Vec3 start, Vec3 top,
                int stages, float first) {
    mj::game::Simulation simulation(level, world);
    simulation.player.place(start, mj::yaw_of(top - start));
    int jumps = 0;
    for (int tick = 0; tick < 900; ++tick) {
        const auto &state = simulation.player.current();
        const float distance = mj::length(mj::horizontal(top - state.position));
        std::uint32_t buttons = mj::input_button::jump_held;
        if (state.grounded && jumps > 0 && jumps < stages && state.position.y < top.y - 1) {
            buttons |= mj::input_button::jump_pressed; // Again, as it lands.
            ++jumps;
        } else if (state.grounded && jumps == 0 && distance <= first) {
            buttons |= mj::input_button::jump_pressed;
            ++jumps;
        }
        const bool last = jumps == stages && !state.grounded;
        simulation.tick(last ? aim(simulation, top, buttons) : toward(simulation, top, 1, buttons),
                        k_dt);
        const auto &after = simulation.player.current();
        if (after.grounded && std::abs(after.position.y - top.y) < 0.15F)
            return true;
        if (jumps == stages && after.grounded && after.position.y < top.y - 1)
            return false; // Fell short and landed below.
        if (simulation.respawns() > 0)
            return false;
    }
    return false;
}

// Whether any takeoff distance lets a chain of `stages` jumps land on the ledge.
bool reachable(const mj::game::Level &level, const mj::CollisionWorld &world, Vec3 start, Vec3 top,
               int stages) {
    for (float first = 2; first <= 22; first += 0.5F) {
        if (chain_onto(level, world, start, top, stages, first))
            return true;
    }
    return false;
}
} // namespace

TEST_CASE("Echo Plateau's three steps need one, two and three chained jumps") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    const Vec3 plateau{29, 0.07F, -35.9F};
    const std::array<Vec3, 3> steps{
        {{22.5F, 2.0F, -44.4F}, {29, 3.2F, -45.9F}, {35.5F, 4.4F, -44.4F}}};
    for (int jumps = 1; jumps <= 3; ++jumps) {
        CAPTURE(jumps);
        const Vec3 step = steps[static_cast<std::size_t>(jumps - 1)];
        const Vec3 start{step.x, plateau.y, plateau.z + 11.5F};
        CHECK(reachable(level, geometry.collision, start, step, jumps));
        if (jumps > 1) // One jump fewer never makes it.
            CHECK_FALSE(reachable(level, geometry.collision, start, step, jumps - 1));
    }
    // The triple step holds the spark.
    mj::game::Simulation simulation(level, geometry.collision);
    simulation.player.place(steps[2] + Vec3{0, 0.02F, 0}, 0);
    for (int tick = 0; tick < 30; ++tick)
        simulation.tick({}, k_dt);
    CHECK(simulation.collectible_collected(spark_index(level, "Third step")));
}

TEST_CASE("the gap to Echo Plateau takes a jump and a dive; a miss falls forward") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    const float dx = 29.0F / std::hypot(29.0F, 35.9F), dz = -35.9F / std::hypot(29.0F, 35.9F);
    const auto along = [&](float d, float y) { return Vec3{dx * d, y, dz * d}; };
    // Across Gate Isle and the two stones, then leap for the plateau.
    const auto cross = [&](bool dive) {
        mj::game::Simulation simulation(level, geometry.collision);
        hop(simulation, geometry.collision, {{along(16.5F, 0.6F)}, {along(23, 1.2F)}});
        const Vec3 landing = along(36, 0.0F);
        bool jumped = false, dove = false;
        for (int tick = 0; tick < 300; ++tick) {
            const auto &state = simulation.player.current();
            std::uint32_t buttons = mj::input_button::jump_held;
            const float out = mj::dot(mj::horizontal(state.position), {dx, 0, dz});
            if (!jumped && state.grounded && out > 24.2F) {
                buttons |= mj::input_button::jump_pressed;
                jumped = true;
            } else if (dive && jumped && !dove && !state.grounded && state.velocity.y < 3) {
                buttons |= mj::input_button::dive_pressed;
                dove = true;
            }
            simulation.tick(toward(simulation, landing, 1, buttons), k_dt);
            if (jumped && simulation.player.current().grounded && tick > 30)
                break;
        }
        return simulation;
    };
    const auto made = cross(true);
    CHECK(made.respawns() == 0);
    CHECK(made.player.current().position.y == doctest::Approx(0).epsilon(0.05));
    CHECK(made.collectible_collected(spark_index(level, "Over the gap")));
    auto missed = cross(false);
    CHECK(missed.respawns() == 0);
    CHECK(missed.player.current().position.y < -3.5F); // On the catch islet below.
    for (int tick = 0; tick < 60; ++tick)              // Come to a stop first.
        missed.tick({0, 0, 0, -0.32F, 0}, k_dt);
    // Its mushroom throws you back up to the last stone.
    const Vec3 mushroom = along(26.2F, -2.1F);
    const Vec3 stone = along(23, 1.2F);
    bool bounced = false;
    for (int tick = 0; tick < 600; ++tick) {
        const auto &state = missed.player.current();
        bounced = bounced || state.state == mj::game::MoveState::bounce;
        std::uint32_t buttons = mj::input_button::jump_held;
        if (!bounced && state.grounded &&
            mj::length(mj::horizontal(mushroom - state.position)) < 3.0F)
            buttons |= mj::input_button::jump_pressed;
        missed.tick(bounced          ? aim(missed, stone, buttons)
                    : state.grounded ? toward(missed, mushroom, 0.5F, buttons)
                                     : aim(missed, mushroom, buttons),
                    k_dt);
        trace("gap", missed);
        if (bounced && missed.player.current().grounded)
            break;
    }
    CHECK(bounced);
    CHECK(missed.respawns() == 0);
    CHECK(missed.player.current().position.y == doctest::Approx(1.2F).epsilon(0.05));
}

TEST_CASE("the grotto under Gate Isle is a drop off the rim, with a mushroom back up") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    const Vec3 out{std::cos(mj::k_pi / 6), 0, std::sin(mj::k_pi / 6)};
    mj::game::Simulation simulation(level, geometry.collision);
    simulation.player.place(out * 7 + Vec3{0, 0.01F, 0}, mj::yaw_of(out));
    for (int tick = 0; tick < 240 && simulation.player.current().position.y > -4.5F; ++tick) {
        simulation.tick(toward(simulation, out * 14, 0.4F), k_dt);
        trace("grotto", simulation);
    }
    for (int tick = 0; tick < 60; ++tick)
        simulation.tick({}, k_dt);
    REQUIRE(simulation.respawns() == 0);
    REQUIRE(simulation.player.current().grounded);
    CHECK(simulation.player.current().position.y == doctest::Approx(-5).epsilon(0.02));
    walk(simulation, out * 9.0F + Vec3{0, -5, 1.2F});
    CHECK(simulation.collectible_collected(spark_index(level, "Grotto glow")));
    // Walk along the shelf, sideways to the rim, onto the spring pad; steer home once above.
    const Vec3 rim{-std::sin(mj::k_pi / 6), 0, std::cos(mj::k_pi / 6)};
    walk(simulation, out * 11.4F + rim * 0.7F + Vec3{0, -5, 0}, 0.4F);
    const Vec3 mushroom = out * 11.4F - rim * 1.6F + Vec3{0, -4.78F, 0};
    bool bounced = false;
    for (int tick = 0; tick < 600; ++tick) {
        const auto &state = simulation.player.current();
        bounced = bounced || state.state == mj::game::MoveState::bounce;
        std::uint32_t buttons = mj::input_button::jump_held;
        const bool clear = bounced && state.position.y > 0.6F; // Above the rim: steer home.
        simulation.tick(clear            ? aim(simulation, out * 6, buttons)
                        : bounced        ? mj::TickInput{0, 0, 0, -0.32F, buttons}
                        : state.grounded ? toward(simulation, mushroom, 0.35F, buttons)
                                         : aim(simulation, mushroom, buttons),
                        k_dt);
        trace("back", simulation);
        if (bounced && simulation.player.current().grounded &&
            simulation.player.current().position.y > -1)
            break;
    }
    CHECK(bounced);
    CHECK(simulation.respawns() == 0);
    CHECK(simulation.player.current().position.y == doctest::Approx(0).epsilon(0.05));
}

TEST_CASE("a grove mushroom throws you up to the Sky Blossom") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    mj::game::Simulation simulation(level, geometry.collision);
    simulation.player.place({-9, 3.01F, -50.5F}, mj::k_pi);
    const Vec3 mushroom{-9, 4.3F, -55};
    const Vec3 canopy{-9, 11.7F, -60};
    bool bounced = false;
    for (int tick = 0; tick < 600; ++tick) {
        const auto &state = simulation.player.current();
        bounced = bounced || state.state == mj::game::MoveState::bounce;
        std::uint32_t buttons = mj::input_button::jump_held;
        if (!bounced && state.grounded &&
            mj::length(mj::horizontal(mushroom - state.position)) < 2.6F)
            buttons |= mj::input_button::jump_pressed;
        // Bounce up beside the canopy, carried on by the run-in, and settle onto it.
        const bool rising = bounced && state.velocity.y > 0;
        simulation.tick(rising           ? toward(simulation, canopy, 0.6F, buttons)
                        : bounced        ? aim(simulation, canopy, buttons)
                        : state.grounded ? toward(simulation, mushroom, 0.6F, buttons)
                                         : aim(simulation, mushroom, buttons),
                        k_dt);
        trace("sky", simulation);
        if (bounced && simulation.player.current().grounded)
            break;
    }
    REQUIRE(bounced);
    CHECK(simulation.respawns() == 0);
    CHECK(simulation.player.current().position.y > 11.0F);
    walk(simulation, canopy, 0.3F);
    CHECK(simulation.collectible_collected(spark_index(level, "Sky blossom")));
}

TEST_CASE("Hermit's Rock is a long jump off the spire's tower, out of reach of a jump") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    const auto leap = [&](bool long_jump) {
        mj::game::Simulation simulation(level, geometry.collision);
        simulation.player.place({-44.4F, 9.01F, -14.4F}, 0);
        bool jumped = false;
        for (int tick = 0; tick < 600; ++tick) {
            const auto &state = simulation.player.current();
            std::uint32_t buttons = mj::input_button::jump_held;
            if (!jumped && state.grounded && state.position.z < -21.6F) {
                buttons |= mj::input_button::jump_pressed;
                if (long_jump)
                    buttons |= mj::input_button::long_jump_held;
                jumped = true;
            }
            simulation.tick({0, 1, 0, -0.32F, buttons}, k_dt);
            if (simulation.respawns() > 0 || (jumped && simulation.player.current().grounded))
                break;
        }
        return simulation;
    };
    const auto made = leap(true);
    CHECK(made.respawns() == 0);
    CHECK(made.player.current().position.y == doctest::Approx(1).epsilon(0.05));
    CHECK(leap(false).respawns() == 1);
    // On the rock: its spark, and a gate home.
    mj::game::Simulation simulation(level, geometry.collision);
    simulation.player.place({-45.6F, 1.01F, -38.7F}, 0);
    for (int tick = 0; tick < 30; ++tick)
        simulation.tick({}, k_dt);
    CHECK(simulation.collectible_collected(spark_index(level, "Hermit's lantern")));
}

TEST_CASE("clean long-jump chains skip across the stones east of Lantern Isle") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    mj::game::Simulation simulation(level, geometry.collision, 5);
    simulation.player.place({1, -3.99F, 54.5F}, mj::k_pi * 0.5F);
    int jumps = 0;
    for (int tick = 0; tick < 900 && jumps < 5; ++tick) {
        const auto &state = simulation.player.current();
        if (jumps == 4 && state.grounded)
            break; // Landed on the far rock: stop chaining.
        std::uint32_t buttons = mj::input_button::jump_held;
        if (state.grounded && (jumps > 0 || state.position.x > 9.1F)) {
            buttons |= mj::input_button::jump_pressed | mj::input_button::long_jump_held;
            ++jumps; // Each landing chains straight into the next long jump.
        }
        simulation.tick({0, 1, mj::k_pi * 0.5F, -0.32F, buttons}, k_dt);
        trace("skip", simulation);
        REQUIRE(simulation.respawns() == 0);
    }
    for (int tick = 0; tick < 120; ++tick)
        simulation.tick({0, 0, 0, -0.32F, 0}, k_dt);
    CHECK(simulation.respawns() == 0);
    CHECK(simulation.player.current().position.x > 48.0F);
    walk(simulation, {50, -4, 53});
    CHECK(simulation.collectible_collected(spark_index(level, "Skipping stones")));
}

TEST_CASE("Echo Plateau leads on to the Old Ruins") {
    const auto level = load("lantern-isles.json");
    const auto geometry = mj::game::build_geometry(level);
    mj::game::Simulation simulation(level, geometry.collision, 6);
    hop(simulation, geometry.collision,
        {{{32, 0, -24}, 1.0F}, {{34.4F, 0.8F, -17.5F}}, {{36, 1.5F, -8.5F}, 1.5F}});
    CHECK(simulation.respawns() == 0);
}
