#include <mj/collision/geometry.hpp>

#include <array>
#include <limits>
#include <utility>

namespace mj {

Vec3 closest_point_on_segment(Vec3 point, const Segment &segment) {
    const Vec3 edge = segment.end - segment.start;
    const float length2 = dot(edge, edge);
    if (length2 <= 1.0e-12F)
        return segment.start;
    const float t = std::clamp(dot(point - segment.start, edge) / length2, 0.0F, 1.0F);
    return segment.start + edge * t;
}

// Voronoi-region walk over the triangle's vertices, edges and face.
Vec3 closest_point_on_triangle(Vec3 point, const Triangle &triangle) {
    const auto &[a, b, c] = triangle;
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = point - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    const Vec3 bp = point - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = point - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denominator = 1.0F / (va + vb + vc);
    return a + ab * (vb * denominator) + ac * (vc * denominator);
}

std::pair<Vec3, Vec3> closest_points_between(const SegmentPair &segments) {
    const auto &[first, second] = segments;
    const Vec3 d1 = first.end - first.start;
    const Vec3 d2 = second.end - second.start;
    const Vec3 r = first.start - second.start;
    const float a = dot(d1, d1);
    const float e = dot(d2, d2);
    const float f = dot(d2, r);
    constexpr float k_epsilon = 1.0e-12F;
    float s = 0;
    float t = 0;
    if (a <= k_epsilon && e <= k_epsilon)
        return {first.start, second.start};
    if (a <= k_epsilon) {
        t = std::clamp(f / e, 0.0F, 1.0F);
    } else {
        const float c = dot(d1, r);
        if (e <= k_epsilon) {
            s = std::clamp(-c / a, 0.0F, 1.0F);
        } else {
            const float b = dot(d1, d2);
            const float denominator = a * e - b * b;
            s = denominator > k_epsilon ? std::clamp((b * f - c * e) / denominator, 0.0F, 1.0F)
                                        : 0.0F;
            t = (b * s + f) / e;
            if (t < 0) {
                t = 0;
                s = std::clamp(-c / a, 0.0F, 1.0F);
            } else if (t > 1) {
                t = 1;
                s = std::clamp((b - c) / a, 0.0F, 1.0F);
            }
        }
    }
    return {first.start + d1 * s, second.start + d2 * t};
}

namespace {
std::optional<Vec3> segment_crossing(const Segment &segment, const Triangle &triangle) {
    const Vec3 normal = cross(triangle.b - triangle.a, triangle.c - triangle.a);
    const float start = dot(segment.start - triangle.a, normal);
    const float end = dot(segment.end - triangle.a, normal);
    if ((start > 0 && end > 0) || (start < 0 && end < 0) || start == end)
        return std::nullopt;
    const Vec3 point = lerp(segment.start, segment.end, start / (start - end));
    // Inside test with consistent edge orientation.
    const std::array<std::pair<Vec3, Vec3>, 3> edges{
        {{triangle.a, triangle.b}, {triangle.b, triangle.c}, {triangle.c, triangle.a}}};
    for (const auto &[from, to] : edges) {
        if (dot(cross(to - from, point - from), normal) < 0)
            return std::nullopt;
    }
    return point;
}
} // namespace

ClosestPoints closest_points(const Segment &segment, const Triangle &triangle) {
    if (const auto crossing = segment_crossing(segment, triangle))
        return {*crossing, *crossing, 0};
    ClosestPoints best;
    best.distance_squared = std::numeric_limits<float>::max();
    auto consider = [&best](Vec3 on_segment, Vec3 on_triangle) {
        const float distance = length_squared(on_segment - on_triangle);
        if (distance < best.distance_squared)
            best = {on_segment, on_triangle, distance};
    };
    consider(segment.start, closest_point_on_triangle(segment.start, triangle));
    consider(segment.end, closest_point_on_triangle(segment.end, triangle));
    const std::array<Segment, 3> edges{
        {{triangle.a, triangle.b}, {triangle.b, triangle.c}, {triangle.c, triangle.a}}};
    for (const auto &edge : edges) {
        const auto [on_segment, on_edge] = closest_points_between({segment, edge});
        consider(on_segment, on_edge);
    }
    return best;
}

std::optional<float> intersect(const Ray &ray, const Triangle &triangle) {
    const Vec3 ab = triangle.b - triangle.a;
    const Vec3 ac = triangle.c - triangle.a;
    const Vec3 p = cross(ray.direction, ac);
    const float determinant = dot(ab, p);
    if (std::abs(determinant) < 1.0e-12F)
        return std::nullopt;
    const float inverse = 1.0F / determinant;
    const Vec3 offset = ray.origin - triangle.a;
    const float u = dot(offset, p) * inverse;
    if (u < 0 || u > 1)
        return std::nullopt;
    const Vec3 q = cross(offset, ab);
    const float v = dot(ray.direction, q) * inverse;
    if (v < 0 || u + v > 1)
        return std::nullopt;
    const float distance = dot(ac, q) * inverse;
    if (distance < 0 || distance > ray.max_distance)
        return std::nullopt;
    return distance;
}

} // namespace mj
