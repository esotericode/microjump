#include <mj/collision/collision_world.hpp>

#include <algorithm>
#include <array>
#include <limits>

namespace mj {
namespace {
constexpr std::uint32_t k_leaf_size = 4;
constexpr int k_advancement_steps = 32;

Aabb swept_bounds(const SweepQuery &query) {
    const Segment segment = query.shape.segment();
    const float pad = query.shape.radius + query.skin;
    const Vec3 padding{pad, pad, pad};
    const Vec3 low = component_min(segment.start, segment.start + query.displacement);
    const Vec3 high = component_max(segment.end, segment.end + query.displacement);
    return {low - padding, high + padding};
}

Segment offset(const Segment &segment, Vec3 by) {
    return {segment.start + by, segment.end + by};
}

struct Contact {
    float gap = 0;
    Vec3 normal;
    Vec3 point;
};
struct ContactQuery {
    const Segment &segment;
    float radius;
    const Triangle &triangle;
    Vec3 face_normal;
};
Contact contact(const ContactQuery &query) {
    const auto closest = closest_points(query.segment, query.triangle);
    const float distance = std::sqrt(closest.distance_squared);
    const Vec3 normal = distance > 1.0e-6F
                            ? (closest.on_segment - closest.on_triangle) * (1.0F / distance)
                            : query.face_normal;
    return {distance - query.radius, normal, closest.on_triangle};
}
} // namespace

CollisionWorld::CollisionWorld(std::span<const Triangle> triangles) {
    for (const auto &triangle : triangles) {
        const Vec3 raw = cross(triangle.b - triangle.a, triangle.c - triangle.a);
        if (length_squared(raw) <= 1.0e-12F)
            continue; // Degenerate triangles cannot provide a contact normal.
        m_triangles.push_back(triangle);
        m_normals.push_back(normalized(raw));
    }
    m_order.resize(m_triangles.size());
    for (std::uint32_t i = 0; i < m_order.size(); ++i)
        m_order[i] = i;
    if (!m_triangles.empty()) {
        m_nodes.reserve(m_triangles.size() * 2);
        build(0, static_cast<std::uint32_t>(m_triangles.size()));
    }
}

std::uint32_t CollisionWorld::build(std::uint32_t begin, std::uint32_t end) {
    const auto index = static_cast<std::uint32_t>(m_nodes.size());
    m_nodes.emplace_back();
    auto center = [this](std::uint32_t triangle) {
        const auto &t = m_triangles[triangle];
        return (t.a + t.b + t.c) * (1.0F / 3.0F);
    };
    Aabb bounds = triangle_bounds(m_triangles[m_order[begin]]);
    Aabb centers{center(m_order[begin]), center(m_order[begin])};
    for (std::uint32_t i = begin; i < end; ++i) {
        bounds = merged(bounds, triangle_bounds(m_triangles[m_order[i]]));
        const Vec3 c = center(m_order[i]);
        centers = merged(centers, {c, c});
    }
    m_nodes[index].bounds = bounds;
    if (end - begin <= k_leaf_size) {
        m_nodes[index].first = begin;
        m_nodes[index].count = end - begin;
        return index;
    }
    const Vec3 extent = centers.max - centers.min;
    const std::size_t axis =
        extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    auto key = [&](std::uint32_t triangle) {
        const Vec3 c = center(triangle);
        return std::array{c.x, c.y, c.z}[axis];
    };
    const std::uint32_t middle = begin + (end - begin) / 2;
    std::nth_element(m_order.begin() + begin, m_order.begin() + middle, m_order.begin() + end,
                     [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); });
    const std::uint32_t left = build(begin, middle);
    const std::uint32_t right = build(middle, end);
    m_nodes[index].first = left;
    m_nodes[index].right = right;
    return index;
}

template <typename Visit>
void CollisionWorld::visit_overlapping(const Aabb &bounds, Visit &&visit) const {
    if (m_nodes.empty())
        return;
    // Median splits keep depth near log2(n / leaf); 64 entries cover any practical level.
    std::array<std::uint32_t, 64> stack{};
    std::size_t size = 0;
    stack[size++] = 0;
    while (size > 0) {
        const Node &node = m_nodes[stack[--size]];
        if (!overlaps(node.bounds, bounds))
            continue;
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i)
                visit(m_order[i]);
        } else if (size + 2 <= stack.size()) {
            stack[size++] = node.first;
            stack[size++] = node.right;
        }
    }
}

std::optional<RayHit> CollisionWorld::raycast(const Ray &ray) const {
    const Vec3 end = ray.origin + ray.direction * ray.max_distance;
    std::optional<RayHit> best;
    visit_overlapping(
        {component_min(ray.origin, end), component_max(ray.origin, end)}, [&](std::uint32_t index) {
            const auto distance = intersect(ray, m_triangles[index]);
            if (!distance || (best && *distance >= best->distance))
                return;
            const Vec3 normal = m_normals[index];
            best = RayHit{*distance, dot(normal, ray.direction) > 0 ? -normal : normal,
                          ray.origin + ray.direction * *distance, index};
        });
    return best;
}

// Conservative advancement per candidate triangle: the segment-triangle distance can shrink
// no faster than the motion length, so stepping by the remaining gap can never tunnel.
std::optional<SweepHit> CollisionWorld::sweep(const SweepQuery &query) const {
    const float travel = length(query.displacement);
    if (travel <= 1.0e-7F)
        return std::nullopt;
    const Vec3 direction = query.displacement * (1.0F / travel);
    const Segment segment = query.shape.segment();
    const float radius = query.shape.radius;
    std::optional<SweepHit> best;
    visit_overlapping(swept_bounds(query), [&](std::uint32_t index) {
        const Vec3 face = m_normals[index];
        if (dot(direction, face) > -1.0e-4F)
            return; // Moving along or out of this face.
        const Triangle &triangle = m_triangles[index];
        float t = 0;
        for (int step = 0; step < k_advancement_steps; ++step) {
            if (best && t >= best->fraction)
                return;
            const auto touch =
                contact({offset(segment, query.displacement * t), radius, triangle, face});
            if (touch.gap <= query.skin) {
                // Separating, or tangential within ~0.06 degrees: sliding along a contact
                // (including over a convex edge) must not block on rounding noise.
                if (dot(touch.normal, direction) >= -1.0e-3F)
                    return;
                best = SweepHit{t, touch.normal, face, touch.point, index};
                return;
            }
            t += (touch.gap - query.skin * 0.5F) / travel;
            if (t >= 1)
                return;
        }
        // Not converged (grazing approach): the current t is still safe.
        const auto touch =
            contact({offset(segment, query.displacement * t), radius, triangle, face});
        if (!best || t < best->fraction)
            best = SweepHit{t, touch.normal, face, touch.point, index};
    });
    return best;
}

Vec3 CollisionWorld::depenetration(const Capsule &shape, float skin) const {
    Vec3 total{};
    for (int pass = 0; pass < 4; ++pass) {
        Capsule moved = shape;
        moved.base += total;
        const Segment segment = moved.segment();
        const float pad = shape.radius + skin;
        const Aabb bounds{segment.start - Vec3{pad, pad, pad}, segment.end + Vec3{pad, pad, pad}};
        Vec3 push{};
        float deepest = 0;
        visit_overlapping(bounds, [&](std::uint32_t index) {
            const auto touch =
                contact({segment, shape.radius, m_triangles[index], m_normals[index]});
            const float needed = skin * 0.5F - touch.gap;
            if (needed > deepest) {
                deepest = needed;
                push = touch.normal * needed;
            }
        });
        if (deepest <= 0)
            break;
        total += push;
    }
    return total;
}

} // namespace mj
