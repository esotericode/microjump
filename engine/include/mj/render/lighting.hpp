#pragma once

#include <mj/math/math.hpp>

#include <cstdint>
#include <string_view>

namespace mj {

// Directional light, ambient and sky for one frame. Radiometric values are linear-light
// "illuminance-like" units; exposure maps them to display brightness before tone mapping.
struct Environment {
    Vec3 sun_direction{0, 1, 0}; // Unit, pointing toward the light.
    Vec3 sun_color{1, 1, 1};     // Linear color times intensity.
    Vec3 sky_zenith;             // Hemisphere ambient (and sky gradient) above...
    Vec3 sky_horizon;            // ...at the horizon, also the fog color...
    Vec3 ground;                 // ...and bounce light from below.
    float exposure = 1;
    float fog_density = 0.006F;
};

// Point light, or a spot light when the outer cone is narrower than pi.
struct LocalLight {
    Vec3 position;
    Vec3 color{1, 1, 1}; // Linear color times intensity (at 1 m).
    float range = 10;    // Falloff reaches zero here.
    Vec3 direction{0, -1, 0};
    float inner_cone = k_pi; // Half-angles in radians.
    float outer_cone = k_pi;
};
inline constexpr std::size_t k_max_local_lights = 16;

enum class ShadowQuality : std::uint8_t { low, medium, high };
[[nodiscard]] std::string_view to_string(ShadowQuality quality);

// Resolution, coverage and filter kernel for each preset.
struct ShadowPreset {
    std::uint32_t resolution;
    float radius;      // Half-size of the square covered around the focus, in meters.
    int kernel_radius; // PCF taps per side are 2 * kernel_radius + 1 bilinear compares.
};
[[nodiscard]] ShadowPreset shadow_preset(ShadowQuality quality);

struct ShadowSettings {
    ShadowQuality quality = ShadowQuality::medium;
    Vec3 focus;                 // World point the shadow map is centered on (the player).
    float constant_bias = 2;    // Depth-bias units (D16: 1/65536 of the light depth range).
    float slope_bias = 2.5F;    // Scaled by the depth slope of each shadow-map texel.
    float normal_offset = 1.5F; // Receiver offset along its normal, in shadow-map texels.
};

struct ShadowProjection {
    Mat4 view_projection; // World to shadow clip space (uv = xy * 0.5 + 0.5, depth 0..1).
    float texel_size = 0; // World meters per shadow-map texel.
    float depth_range = 0;
};

// Orthographic sun projection around the focus. The center snaps to whole texels in light
// space so camera/player motion does not make shadow edges shimmer.
[[nodiscard]] ShadowProjection fit_shadow(Vec3 sun_direction, const ShadowSettings &settings);

} // namespace mj
