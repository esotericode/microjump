#include <mj/scene/mesh.hpp>

#include <algorithm>
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

void SceneMesh::add_ramp(const Ramp &ramp) {
    const auto &[center, size, color, yaw] = ramp;
    auto place = [&](float x, float y, float z) {
        return center + Vec3{x * std::cos(yaw) + z * std::sin(yaw), y,
                             -x * std::sin(yaw) + z * std::cos(yaw)};
    };
    const float hx = size.x * 0.5F;
    const float hy = size.y * 0.5F;
    const float hz = size.z * 0.5F;
    // Bottom corners, then the high edge at local -Z.
    const std::array<Vec3, 6> p{place(-hx, -hy, hz),  place(hx, -hy, hz),  place(hx, -hy, -hz),
                                place(-hx, -hy, -hz), place(-hx, hy, -hz), place(hx, hy, -hz)};
    const Vec3 inside = place(0, -hy * 0.5F, -hz * 0.5F);
    auto face = [&](std::initializer_list<std::size_t> corners) {
        std::vector<Vec3> polygon;
        for (const auto corner : corners)
            polygon.push_back(p[corner]);
        Vec3 normal = normalized(cross(polygon[1] - polygon[0], polygon[2] - polygon[0]));
        if (dot(normal, polygon[0] - inside) < 0) {
            std::ranges::reverse(polygon);
            normal = -normal;
        }
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            for (const auto &point : {polygon[0], polygon[i], polygon[i + 1]})
                vertices.push_back({point, normal, color});
        }
    };
    face({0, 1, 2, 3}); // Bottom.
    face({3, 2, 5, 4}); // High vertical back.
    face({0, 1, 5, 4}); // Slope.
    face({0, 3, 4});    // Sides.
    face({1, 2, 5});
}

void SceneMesh::add_capsule(const CapsuleMesh &capsule) {
    constexpr int k_sides = 24;
    constexpr int k_rings = 6; // Per hemisphere.
    const float cylinder = std::max(capsule.height - 2 * capsule.radius, 0.0F);
    const Vec3 bottom = capsule.base + Vec3{0, capsule.radius, 0};
    // Latitude rows from the bottom pole to the top pole; the cylinder joins the equators.
    std::vector<std::pair<float, float>> rows; // (angle from equator, center offset)
    for (int ring = 0; ring <= k_rings; ++ring)
        rows.emplace_back(-k_pi * 0.5F * static_cast<float>(k_rings - ring) / k_rings, 0.0F);
    for (int ring = 0; ring <= k_rings; ++ring)
        rows.emplace_back(k_pi * 0.5F * static_cast<float>(ring) / k_rings, cylinder);
    auto point = [&](std::size_t row, int side) {
        const auto [latitude, lift] = rows[row];
        const float longitude = k_tau * static_cast<float>(side) / k_sides;
        const Vec3 normal{std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                          std::cos(latitude) * std::sin(longitude)};
        return Vertex{bottom + Vec3{0, lift, 0} + normal * capsule.radius, normal, capsule.color};
    };
    for (std::size_t row = 0; row + 1 < rows.size(); ++row) {
        for (int side = 0; side < k_sides; ++side) {
            const auto a = point(row, side);
            const auto b = point(row, side + 1);
            const auto c = point(row + 1, side + 1);
            const auto d = point(row + 1, side);
            // Counter-clockwise seen from outside.
            for (const auto &vertex : {a, d, c, a, c, b}) {
                vertices.push_back(vertex);
            }
        }
    }
}

std::vector<Triangle> SceneMesh::triangles() const {
    std::vector<Triangle> result;
    result.reserve(vertices.size() / 3);
    for (std::size_t i = 0; i + 2 < vertices.size(); i += 3)
        result.push_back(
            {vertices[i].position, vertices[i + 1].position, vertices[i + 2].position});
    return result;
}

} // namespace mj
