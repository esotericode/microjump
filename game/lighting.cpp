#include "lighting.hpp"

namespace mj::game {
namespace {
// The sun rises in +X, sets in -X and arcs 35 degrees toward +Z (the yard's south side).
Vec3 sun_path(float hours) {
    const float angle = (hours - 6) / 12 * k_pi;
    constexpr float k_tilt = 0.61F;
    return normalized(Vec3{std::cos(angle), std::sin(angle) * std::cos(k_tilt),
                           std::sin(angle) * std::sin(k_tilt)});
}
float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3 - 2 * t);
}
Vec3 mix(Vec3 a, Vec3 b, float t) {
    return lerp(a, b, t);
}
// Keeps the shading/shadow direction at least ~7 degrees above the horizon.
Vec3 lift(Vec3 direction) {
    const Vec3 flat = horizontal(direction);
    if (direction.y >= 0.12F || length_squared(flat) < 1.0e-6F)
        return direction;
    return normalized(normalized(flat) * 0.993F + Vec3{0, 0.12F, 0});
}
} // namespace

float daylight(float hours) {
    return smoothstep(-0.1F, 0.15F, sun_path(hours).y);
}

Environment environment_at(const DayCycle &cycle) {
    const Vec3 sun = sun_path(cycle.hours);
    const float day = daylight(cycle.hours);
    const float low = 1 - smoothstep(0.0F, 0.4F, std::abs(sun.y)); // Near the horizon.
    Environment environment;
    if (sun.y > -0.02F) {
        const Vec3 tint = mix(Vec3{1.0F, 0.95F, 0.88F}, Vec3{1.0F, 0.52F, 0.24F}, low);
        environment.sun_direction = lift(sun);
        environment.sun_color = tint * (3.4F * smoothstep(-0.02F, 0.12F, sun.y));
    } else {
        const Vec3 moon = -sun;
        environment.sun_direction = lift(moon);
        environment.sun_color =
            Vec3{0.55F, 0.66F, 0.95F} * (0.28F * smoothstep(0.02F, 0.15F, moon.y));
    }
    const Vec3 day_zenith{0.16F, 0.29F, 0.56F};
    const Vec3 day_horizon{0.46F, 0.56F, 0.66F};
    const Vec3 dusk_zenith{0.10F, 0.12F, 0.26F};
    const Vec3 dusk_horizon{0.62F, 0.36F, 0.22F};
    const Vec3 night_zenith{0.006F, 0.009F, 0.022F};
    const Vec3 night_horizon{0.02F, 0.026F, 0.045F};
    const Vec3 zenith = mix(night_zenith, mix(day_zenith, dusk_zenith, low), day);
    const Vec3 horizon = mix(night_horizon, mix(day_horizon, dusk_horizon, low), day);
    environment.sky_zenith = zenith;
    environment.sky_horizon = horizon;
    environment.ground = mix(Vec3{0.012F, 0.013F, 0.016F}, Vec3{0.17F, 0.16F, 0.14F}, day);
    // A simple stand-in for eye adaptation: brighter exposure as the scene darkens.
    environment.exposure = cycle.exposure_scale * (1.0F + 2.2F * (1 - day));
    environment.fog_density = 0.006F + 0.004F * (1 - day);
    return environment;
}

std::vector<LocalLight> animate_lights(std::span<const LightFixture> fixtures, LightClock clock) {
    const float darkness = 1 - daylight(clock.hours);
    std::vector<LocalLight> lights;
    for (const auto &fixture : fixtures) {
        const float strength = fixture.night_only ? smoothstep(0.2F, 0.7F, darkness) : 1.0F;
        if (strength <= 0)
            continue;
        LocalLight light;
        light.position = fixture.position;
        if (fixture.orbit_radius > 0) {
            const float angle = clock.seconds * fixture.orbit_speed;
            light.position += Vec3{std::cos(angle), 0, std::sin(angle)} * fixture.orbit_radius;
        }
        light.color = fixture.color * (fixture.intensity * strength);
        light.range = fixture.range;
        light.direction = fixture.direction;
        light.inner_cone = fixture.inner_cone;
        light.outer_cone = fixture.outer_cone;
        lights.push_back(light);
    }
    return lights;
}

} // namespace mj::game
