#pragma once

#include <mj/render/lighting.hpp>

#include <span>
#include <vector>

namespace mj::game {

// A light placed by the level. Orbiting fixtures circle `orbit_center` (moving-light demo);
// night-only fixtures (street lamps) fade in as the sun sets.
struct LightFixture {
    Vec3 position;
    Vec3 color{1, 1, 1}; // Linear.
    float intensity = 10;
    float range = 10;
    Vec3 direction{0, -1, 0};
    float inner_cone = k_pi; // Half-angles; pi means a point light.
    float outer_cone = k_pi;
    float orbit_radius = 0;
    float orbit_speed = 0; // Radians per second.
    bool night_only = false;
};

struct DayCycle {
    float hours = 14.5F; // 0..24; 12 is noon, 0 midnight.
    bool animate = false;
    float day_seconds = 180; // Real seconds per full 24 h cycle while animating.
    float exposure_scale = 1;
    void advance(float dt) {
        if (animate && day_seconds > 0)
            hours = std::fmod(hours + dt * 24 / day_seconds + 24, 24.0F);
    }
};

// Everything the lighting section of the tuning window edits.
struct LightingControls {
    DayCycle cycle;
    ShadowSettings shadows;
    bool local_lights = true;
    bool bulbs = true;
    bool blob = true;
    float blob_strength = 0.35F;
};

// 0 at night, 1 in full day, smooth through dawn and dusk.
[[nodiscard]] float daylight(float hours);
[[nodiscard]] Environment environment_at(const DayCycle &cycle);
struct LightClock {
    float seconds = 0; // Animation time (orbits).
    float hours = 12;  // Time of day (night-only fixtures fade in after sunset).
};
[[nodiscard]] std::vector<LocalLight> animate_lights(std::span<const LightFixture> fixtures,
                                                     LightClock clock);

} // namespace mj::game
