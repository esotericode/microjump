#include "portal_visuals.hpp"

#include <cmath>

namespace mj::game {
namespace {
constexpr float k_tube = 0.075F; // Ring thickness relative to its radius.
// The opening is a tall oval: wide enough to run through, tall enough to jump through.
constexpr float k_wide = 0.85F;
constexpr float k_tall = 1.2F;

Mat4 scaled(const Mat4 &frame, Vec3 scale) {
    Mat4 result = frame;
    for (std::size_t row = 0; row < 3; ++row) {
        result.at(row, 0) *= scale.x;
        result.at(row, 1) *= scale.y;
        result.at(row, 2) *= scale.z;
    }
    return result;
}
} // namespace

SceneMesh make_portal_ring_mesh() {
    SceneMesh mesh;
    mesh.surface = {0.25F, 0};
    constexpr int k_segments = 48;
    constexpr int k_sides = 10;
    const auto at = [](int segment, int side) {
        const float around = k_tau * static_cast<float>(segment) / k_segments;
        const float tube = k_tau * static_cast<float>(side) / k_sides;
        const Vec3 out{std::cos(around), std::sin(around), 0};
        const Vec3 normal = out * std::cos(tube) + Vec3{0, 0, std::sin(tube)};
        return Vertex{out + normal * k_tube, normal, {1, 1, 1}, {0.25F, 0}};
    };
    for (int segment = 0; segment < k_segments; ++segment) {
        for (int side = 0; side < k_sides; ++side) {
            const auto a = at(segment, side);
            const auto b = at(segment + 1, side);
            const auto c = at(segment + 1, side + 1);
            const auto d = at(segment, side + 1);
            // Counter-clockwise seen from outside the tube.
            for (const auto &vertex : {a, b, c, a, c, d})
                mesh.vertices.push_back(vertex);
        }
    }
    return mesh;
}

PortalLook portal_look(const Portal &portal, float seconds) {
    const Mat4 frame = yaw_translation(portal.yaw, portal.position);
    const float r = portal.radius;
    const float breathe = 1 + 0.025F * std::sin(seconds * 1.7F);
    PortalLook look;
    look.ring = scaled(frame, Vec3{k_wide, k_tall, 1} * (r * breathe));
    look.veil = scaled(frame, {r * k_wide * 0.97F, r * k_tall * 0.97F, 0.05F});
    look.glow = 0.75F + 0.25F * std::sin(seconds * 2.4F);
    return look;
}

void portal_motes(const Portal &portal, float seconds, std::vector<ParticleInstance> &out) {
    constexpr int k_motes = 14;
    const Vec3 right = yaw_right(portal.yaw);
    const Vec3 facing = yaw_forward(portal.yaw);
    for (int i = 0; i < k_motes; ++i) {
        const float offset = static_cast<float>(i) * 0.618034F;
        const float life = std::fmod(seconds * 0.45F + offset, 1.0F);
        const float angle = static_cast<float>(i) * k_tau / k_motes + seconds * 0.6F + life * 1.2F;
        const float reach = portal.radius * (1.25F - life * 0.95F);
        const float bulge = std::sin(life * k_pi);
        const Vec3 position = portal.position + right * (std::cos(angle) * reach * k_wide) +
                              Vec3{0, std::sin(angle) * reach * k_tall, 0} +
                              facing * (bulge * 0.35F * (i % 2 == 0 ? 1.0F : -1.0F));
        out.push_back({.position = position,
                       .radius = 0.09F + 0.05F * bulge,
                       .color = {0.62F, 0.86F, 1},
                       .opacity = bulge * 0.85F,
                       .emissive = 1.6F});
    }
}

} // namespace mj::game
