#include "spark_visuals.hpp"

#include <array>

namespace mj::game {

SceneMesh make_spark_mesh() {
    SceneMesh mesh;
    mesh.surface = {0.3F, 0.45F};
    constexpr std::array<Vec3, 4> rim{
        {{0.28F, 0, 0}, {0, 0, -0.28F}, {-0.28F, 0, 0}, {0, 0, 0.28F}}};
    constexpr Vec3 top{0, 0.48F, 0}, bottom{0, -0.48F, 0};
    constexpr Vec3 gold{1, 0.57F, 0.08F}, light{1, 0.83F, 0.3F};
    const auto triangle = [&](const std::array<Vec3, 3> &points, Vec3 color) {
        const Vec3 normal = normalized(cross(points[1] - points[0], points[2] - points[0]));
        for (const auto &point : points)
            mesh.vertices.push_back({point, normal, color, mesh.surface});
    };
    for (std::size_t i = 0; i < rim.size(); ++i) {
        const Vec3 a = rim[i], b = rim[(i + 1) % rim.size()];
        triangle({top, a, b}, i % 2 == 0 ? light : gold);
        triangle({bottom, b, a}, i % 2 == 0 ? gold : light);
    }
    return mesh;
}

} // namespace mj::game
