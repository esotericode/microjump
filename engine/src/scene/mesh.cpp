#include <mj/scene/mesh.hpp>

#include <array>

namespace mj {

void SceneMesh::add_box(const Box &box) {
    const auto &[center, size, color, yaw] = box;
    std::array<Vec3, 8> points;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float x = (i & 1U) != 0 ? size.x * 0.5F : -size.x * 0.5F;
        const float y = (i & 2U) != 0 ? size.y * 0.5F : -size.y * 0.5F;
        const float z = (i & 4U) != 0 ? size.z * 0.5F : -size.z * 0.5F;
        points[i] = center + Vec3{x * std::cos(yaw) + z * std::sin(yaw), y,
                                  -x * std::sin(yaw) + z * std::cos(yaw)};
    }
    constexpr std::array<std::array<std::size_t, 4>, 6> faces{
        {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}}};
    for (const auto &face : faces) {
        const Vec3 normal =
            normalized(cross(points[face[1]] - points[face[0]], points[face[2]] - points[face[0]]));
        for (const auto index : std::array{face[0], face[1], face[2], face[0], face[2], face[3]}) {
            vertices.push_back({points[index], normal, color});
        }
    }
}

void SceneMesh::add_cylinder(Vec3 center, float radius, float height, Vec3 color) {
    constexpr int k_sides = 32;
    for (int i = 0; i < k_sides; ++i) {
        const float a = 6.2831853F * static_cast<float>(i) / k_sides;
        const float b = 6.2831853F * static_cast<float>(i + 1) / k_sides;
        const Vec3 p0 = center + Vec3{radius * std::cos(a), -height * 0.5F, radius * std::sin(a)};
        const Vec3 p1 = center + Vec3{radius * std::cos(b), -height * 0.5F, radius * std::sin(b)};
        const Vec3 p2 = p1 + Vec3{0, height, 0};
        const Vec3 p3 = p0 + Vec3{0, height, 0};
        const Vec3 normal = normalized(Vec3{std::cos((a + b) * 0.5F), 0, std::sin((a + b) * 0.5F)});
        for (const Vec3 p : std::array{p0, p3, p2, p0, p2, p1})
            vertices.push_back({p, normal, color});
        vertices.push_back({center + Vec3{0, height * 0.5F, 0}, {0, 1, 0}, color});
        vertices.push_back({p2, {0, 1, 0}, color});
        vertices.push_back({p3, {0, 1, 0}, color});
        vertices.push_back({center + Vec3{0, -height * 0.5F, 0}, {0, -1, 0}, color});
        vertices.push_back({p0, {0, -1, 0}, color});
        vertices.push_back({p1, {0, -1, 0}, color});
    }
}

} // namespace mj
