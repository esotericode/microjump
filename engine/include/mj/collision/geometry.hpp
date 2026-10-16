#pragma once

#include <mj/math/math.hpp>

#include <optional>
#include <utility>

namespace mj {

struct Triangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    bool wall_jump = false; // Authored designation; only near-vertical faces can be kicked.
};
struct Segment {
    Vec3 start;
    Vec3 end;
};
struct ClosestPoints {
    Vec3 on_segment;
    Vec3 on_triangle;
    float distance_squared = 0;
};
struct Ray {
    Vec3 origin;
    Vec3 direction; // Unit length.
    float max_distance = 0;
};

[[nodiscard]] inline Vec3 triangle_normal(const Triangle &triangle) {
    return normalized(cross(triangle.b - triangle.a, triangle.c - triangle.a));
}
[[nodiscard]] inline Aabb triangle_bounds(const Triangle &triangle) {
    return {component_min(triangle.a, component_min(triangle.b, triangle.c)),
            component_max(triangle.a, component_max(triangle.b, triangle.c))};
}

[[nodiscard]] Vec3 closest_point_on_segment(Vec3 point, const Segment &segment);
[[nodiscard]] Vec3 closest_point_on_triangle(Vec3 point, const Triangle &triangle);
// Closest points between two segments; first member lies on `first`.
struct SegmentPair {
    Segment first;
    Segment second;
};
[[nodiscard]] std::pair<Vec3, Vec3> closest_points_between(const SegmentPair &segments);
[[nodiscard]] ClosestPoints closest_points(const Segment &segment, const Triangle &triangle);
// Two-sided ray/triangle intersection distance along the ray.
[[nodiscard]] std::optional<float> intersect(const Ray &ray, const Triangle &triangle);

} // namespace mj
