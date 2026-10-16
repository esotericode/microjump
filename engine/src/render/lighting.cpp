#include <mj/render/lighting.hpp>

namespace mj {

std::string_view to_string(ShadowQuality quality) {
    switch (quality) {
    case ShadowQuality::low:
        return "Low";
    case ShadowQuality::medium:
        return "Medium";
    case ShadowQuality::high:
        return "High";
    }
    return "?";
}

ShadowPreset shadow_preset(ShadowQuality quality) {
    switch (quality) {
    case ShadowQuality::low:
        return {1024, 30, 0};
    case ShadowQuality::medium:
        return {2048, 40, 1};
    case ShadowQuality::high:
        return {4096, 45, 2};
    }
    return {2048, 40, 1};
}

ShadowProjection fit_shadow(Vec3 sun_direction, const ShadowSettings &settings) {
    const auto preset = shadow_preset(settings.quality);
    const float radius = preset.radius;
    const float texel = 2 * radius / static_cast<float>(preset.resolution);
    // The light looks along -sun_direction; casters up to `height` above the focus fit.
    constexpr float k_height = 60;
    const Vec3 light = normalized(sun_direction);
    const Mat4 rotation = look_at({{0, 0, 0}, -light});
    // Snap the focus to the texel grid in light space, then back to world space.
    const auto local =
        transform(rotation, {settings.focus.x, settings.focus.y, settings.focus.z, 1});
    const float snapped_x = std::round(local[0] / texel) * texel;
    const float snapped_y = std::round(local[1] / texel) * texel;
    const Mat4 back = inverse(rotation);
    const auto world = transform(back, {snapped_x, snapped_y, local[2], 1});
    const Vec3 center{world[0], world[1], world[2]};
    const float depth_range = 2 * radius + k_height * 2;
    const Vec3 eye = center + light * (radius + k_height);
    const Mat4 view = look_at({eye, center});
    const Mat4 projection = orthographic({radius, radius, 0, depth_range});
    return {projection * view, texel, depth_range};
}

} // namespace mj
