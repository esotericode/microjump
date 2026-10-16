#pragma once

#include <mj/math/math.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace mj {

// A camera-facing disc, blended in linear HDR after opaque geometry. Its soft radial
// edge uses straight alpha; ordinary depth testing clips intersections with the world.
struct ParticleInstance {
    Vec3 position;
    float radius = 0;    // Metres: the half width of the billboard. Zero disables it.
    Vec3 color{1, 1, 1}; // Linear reflectance, multiplied by ambient light + emission.
    float opacity = 1;   // Straight alpha, clamped to [0, 1].
    float emissive = 0;
};

inline constexpr std::size_t k_max_particles = 256;

// Considers at most the first k_max_particles entries, dropping non-finite/invalid
// geometry, negative radiance and invisible entries. Returns a stable back-to-front
// order along the camera's forward axis; equal-depth particles retain submission order.
[[nodiscard]] std::vector<ParticleInstance>
prepare_particles(std::span<const ParticleInstance> particles, Vec3 camera_position,
                  Vec3 camera_forward);

} // namespace mj
