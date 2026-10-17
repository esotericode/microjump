#pragma once

// Plays level routes the way a player does: walk, line up and hop, ride a moving platform,
// wait for the right moment. Shared by the shipped levels' route tests.

#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace route {
using mj::Vec3;
inline constexpr float k_dt = 1.0F / 120;
// Set to true to print every tick of the traced routes, when a new route won't pass.
inline constexpr bool k_route_log = false;

inline void trace(const char *tag, const mj::game::Simulation &simulation) {
    if (!k_route_log)
        return;
    const auto &s = simulation.player.current();
    std::printf("%s t%llu %.2f %.2f %.2f v %.2f %.2f %.2f %s %s r%u b%d\n", tag,
                static_cast<unsigned long long>(simulation.ticks()), double(s.position.x),
                double(s.position.y), double(s.position.z), double(s.velocity.x),
                double(s.velocity.y), double(s.velocity.z), s.grounded ? "G" : "A",
                mj::game::to_string(s.state), simulation.respawns(),
                s.ground_body ? static_cast<int>(*s.ground_body) : -1);
}

inline mj::game::Level load(std::string_view name) {
    auto level = mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / name);
    REQUIRE(level);
    return std::move(*level);
}

inline std::size_t spark_index(const mj::game::Level &level, std::string_view name) {
    for (std::size_t i = 0; i < level.collectibles.size(); ++i) {
        if (level.collectibles[i].name == name)
            return i;
    }
    FAIL("Missing authored spark: " << name);
    return level.collectibles.size();
}

inline std::size_t mover_index(const mj::game::Level &level, std::string_view name) {
    for (std::size_t i = 0; i < level.movers.size(); ++i) {
        if (level.movers[i].name == name)
            return i;
    }
    FAIL("Missing authored mover: " << name);
    return level.movers.size();
}

// A camera-relative stick (camera yaw 0) pointing at `target`.
inline mj::TickInput toward(const mj::game::Simulation &simulation, Vec3 target, float strength = 1,
                            std::uint32_t buttons = mj::input_button::jump_held) {
    const Vec3 offset = mj::horizontal(target - simulation.player.current().position);
    const float distance = mj::length(offset);
    if (distance < 1e-3F)
        return {0, 0, 0, -0.32F, buttons};
    const float scale = strength * std::min(1.0F, distance / 0.6F) / distance;
    return {offset.x * scale, -offset.z * scale, 0, -0.32F, buttons};
}

// In the air, steer for the velocity that touches down on `top` just as the jump falls to its
// height, as a player lines up a landing instead of pushing at it. `drift` is how the landing
// itself is moving.
inline mj::TickInput aim(const mj::game::Simulation &simulation, Vec3 top, std::uint32_t buttons,
                         Vec3 drift = {}) {
    const auto &state = simulation.player.current();
    const auto &tuning = simulation.player.tuning;
    const float gravity = tuning.gravity() * tuning.fall_gravity_scale;
    const float drop = state.position.y - top.y;
    const float reach = state.velocity.y * state.velocity.y + 2 * gravity * drop;
    if (reach <= 0)
        return toward(simulation, top, 1, buttons); // Still below it: just press on.
    const float time = std::max((state.velocity.y + std::sqrt(reach)) / gravity, 0.05F);
    const Vec3 wanted = mj::horizontal(top + drift * time - state.position) * (1 / time);
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
// On a mover, `top` is in the mover's own space and the landing counts only on that mover.
struct Landing {
    Vec3 top;
    float radius = 1.2F;
    // Small tops (columns) want a gentle hop: settle below this speed before leaping.
    float max_speed = 100;
    std::optional<std::size_t> mover = std::nullopt;
};

// Where a landing is now, and how fast it is moving.
inline Vec3 landing_point(const mj::game::Simulation &simulation, const Landing &landing) {
    return landing.mover ? mj::to_world(simulation.mover_pose(*landing.mover, 1), landing.top)
                         : landing.top;
}
inline Vec3 landing_drift(const mj::game::Simulation &simulation, const Landing &landing) {
    if (!landing.mover)
        return {};
    return simulation.scene()
               .carry(static_cast<std::uint32_t>(*landing.mover),
                      landing_point(simulation, landing))
               .offset *
           (1 / k_dt);
}
inline bool landed_on(const mj::game::Simulation &simulation, const Landing &landing) {
    const auto &state = simulation.player.current();
    if (!state.grounded)
        return false;
    if (landing.mover && state.ground_body != static_cast<std::uint32_t>(*landing.mover))
        return false;
    const Vec3 top = landing_point(simulation, landing);
    return std::abs(state.position.y - top.y) < 0.3F &&
           mj::length(mj::horizontal(top - state.position)) < landing.radius;
}

// Seconds from a held jump's takeoff until it falls back to `rise` above the takeoff.
inline float flight_time(const mj::game::PlayerTuning &tuning, float rise) {
    const float gravity = tuning.gravity();
    const float launch = tuning.jump_velocity();
    const float apex = launch * launch / (2 * gravity);
    const float fall = std::max(apex - rise, 0.0F);
    return launch / gravity + std::sqrt(2 * fall / (gravity * tuning.fall_gravity_scale));
}

// Plays each hop the way a player judges one: turn toward the landing, take off once the
// current speed would carry the jump there (or at the edge of the ground underfoot), then
// line up the touchdown in the air. Fails on any fall: every hop on a shipped route must be
// an ordinary jump. A moving landing is aimed at where it will be.
inline void hop(mj::game::Simulation &simulation, std::initializer_list<Landing> landings,
                bool allow_falls = false) {
    int index = 0;
    for (const auto &landing : landings) {
        CAPTURE(index);
        bool arrived = false;
        for (int tick = 0; tick < 900 && !arrived; ++tick) {
            const auto scene = simulation.scene();
            const auto &state = simulation.player.current();
            const Vec3 target = landing_point(simulation, landing);
            const Vec3 offset = mj::horizontal(target - state.position);
            const float distance = mj::length(offset);
            // Ground all the way there (within a step) means walk; any gap means hop.
            bool connected = std::abs(state.position.y - target.y) < 0.35F &&
                             (!landing.mover || state.ground_body == landing.mover);
            for (float along = 0.4F; connected && along < distance; along += 0.4F) {
                const Vec3 probe = state.position + offset * (along / distance);
                const auto floor = scene.raycast({probe + Vec3{0, 0.5F, 0}, {0, -1, 0}, 1.0F});
                connected = floor && std::abs(floor->point.y - state.position.y) < 0.35F;
            }
            const Vec3 ahead = state.position + offset * (0.5F / std::max(distance, 0.5F));
            const auto ground = scene.raycast({ahead + Vec3{0, 0.5F, 0}, {0, -1, 0}, 1.0F});
            const bool edge = !ground || ground->point.y < state.position.y - 0.6F;
            const Vec3 moving = mj::horizontal(state.velocity);
            const float speed = mj::length(moving);
            const bool facing = speed < 1.0F || mj::dot(moving, offset) > 0.95F * speed * distance;
            const float carry =
                speed * flight_time(simulation.player.tuning, target.y - state.position.y);
            std::uint32_t buttons = mj::input_button::jump_held;
            mj::TickInput input;
            if (!state.grounded) {
                input = aim(simulation, target, buttons, landing_drift(simulation, landing));
            } else if (connected) {
                input = toward(simulation, target, 0.6F, buttons); // Walk there.
            } else if (speed > landing.max_speed) {
                input = {0, 0, 0, -0.32F, buttons}; // Settle before a careful hop.
            } else {
                if ((facing && carry >= distance - 0.4F) || edge)
                    buttons |= mj::input_button::jump_pressed;
                // Ease off while still turning, so the turn finishes before the edge.
                input = toward(simulation, target, facing ? 1.0F : 0.4F, buttons);
            }
            simulation.tick(input, k_dt);
            trace("hop", simulation);
            arrived = landed_on(simulation, landing);
            if (!allow_falls)
                REQUIRE(simulation.respawns() == 0);
        }
        REQUIRE(arrived);
        ++index;
    }
}

// Walks to `target` without jumping, then stands still long enough to settle.
inline void walk(mj::game::Simulation &simulation, Vec3 target, float strength = 0.6F) {
    for (int tick = 0; tick < 600; ++tick) {
        if (mj::length(mj::horizontal(target - simulation.player.current().position)) < 0.3F)
            break;
        simulation.tick(toward(simulation, target, strength), k_dt);
        trace("walk", simulation);
    }
    for (int tick = 0; tick < 30; ++tick)
        simulation.tick({}, k_dt);
}

// Stands (riding whatever is underfoot) until `ready`, for at most `seconds`. Returns whether
// the moment came. On a mover, keeps to `spot` in its space.
inline bool wait_until(mj::game::Simulation &simulation,
                       const std::function<bool(const mj::game::Simulation &)> &ready,
                       float seconds, std::optional<Landing> spot = std::nullopt) {
    for (int tick = 0; tick < static_cast<int>(seconds / k_dt); ++tick) {
        if (ready(simulation))
            return true;
        mj::TickInput input{0, 0, 0, -0.32F, mj::input_button::jump_held};
        if (spot) {
            const Vec3 target = landing_point(simulation, *spot);
            if (mj::length(mj::horizontal(target - simulation.player.current().position)) > 0.3F)
                input = toward(simulation, target, 0.4F);
        }
        simulation.tick(input, k_dt);
        trace("wait", simulation);
    }
    return ready(simulation);
}

// True once mover `index` has come within `distance` of `station` (its pivot's position).
inline std::function<bool(const mj::game::Simulation &)> mover_near(std::size_t index, Vec3 station,
                                                                    float distance = 0.05F) {
    return [=](const mj::game::Simulation &simulation) {
        return mj::length(simulation.mover_pose(index, 1).position - station) <= distance;
    };
}
} // namespace route
