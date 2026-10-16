#include <mj/render/particles.hpp>

#include <algorithm>
#include <cmath>

namespace mj {
namespace {
bool finite(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

std::vector<ParticleInstance> prepare_particles(std::span<const ParticleInstance> particles,
                                                Vec3 camera_position, Vec3 camera_forward) {
    std::vector<ParticleInstance> sorted;
    if (!finite(camera_position) || !finite(camera_forward))
        return sorted;
    const auto count = std::min(particles.size(), k_max_particles);
    sorted.reserve(count);
    const auto depth = [&](const ParticleInstance &particle) {
        return dot(particle.position - camera_position, camera_forward);
    };
    for (const auto &particle : particles.first(count)) {
        if (!finite(particle.position) || !finite(particle.color) ||
            !std::isfinite(particle.radius) || particle.radius <= 0 ||
            !std::isfinite(particle.opacity) || particle.opacity <= 0 ||
            !std::isfinite(particle.emissive) || particle.emissive < 0 || particle.color.x < 0 ||
            particle.color.y < 0 || particle.color.z < 0 || !std::isfinite(depth(particle)))
            continue;
        sorted.push_back(particle);
        sorted.back().opacity = std::min(particle.opacity, 1.0F);
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const auto &a, const auto &b) { return depth(a) > depth(b); });
    return sorted;
}

} // namespace mj
