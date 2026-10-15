#pragma once

#include <mj/collision/geometry.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mj {

// Upright capsule. `base` is the lowest point; height includes both hemispheres.
struct Capsule {
    Vec3 base;
    float radius = 0.35F;
    float height = 1.6F;

    [[nodiscard]] Segment segment() const {
        const float half_cylinder = std::max(height - 2 * radius, 0.0F);
        const Vec3 bottom = base + Vec3{0, radius, 0};
        return {bottom, bottom + Vec3{0, half_cylinder, 0}};
    }
};

struct SweepQuery {
    Capsule shape;
    Vec3 displacement;
    // Motion stops this far (approximately, never closer than half) from any surface.
    float skin = 0.01F;
};
struct SweepHit {
    float fraction = 0; // Safe fraction of the displacement.
    Vec3 normal;        // Unit contact normal, from the surface toward the capsule.
    Vec3 face_normal;   // Normal of the triangle that was hit (differs at edges).
    Vec3 point;         // Closest surface point at the stop position.
    std::uint32_t triangle = 0;
};
struct RayHit {
    float distance = 0;
    Vec3 normal; // Faces the ray origin.
    Vec3 point;
    std::uint32_t triangle = 0;
};

// Static world geometry: a triangle soup with a bounding volume hierarchy.
// Queries are one-sided: triangles face outward (counter-clockwise winding) and only
// block motion entering their front side, which matches closed generated primitives.
class CollisionWorld {
  public:
    CollisionWorld() = default;
    explicit CollisionWorld(std::span<const Triangle> triangles);

    [[nodiscard]] std::optional<RayHit> raycast(const Ray &ray) const;
    [[nodiscard]] std::optional<SweepHit> sweep(const SweepQuery &query) const;
    // Offset that moves an overlapping capsule at least `skin` / 2 away from all surfaces.
    [[nodiscard]] Vec3 depenetration(const Capsule &shape, float skin) const;
    [[nodiscard]] std::span<const Triangle> triangles() const {
        return m_triangles;
    }

  private:
    struct Node {
        Aabb bounds;
        std::uint32_t first = 0; // Left child (interior) or first triangle slot (leaf).
        std::uint32_t right = 0; // Right child (interior only).
        std::uint32_t count = 0; // Triangle count; zero for interior nodes.
    };
    std::uint32_t build(std::uint32_t begin, std::uint32_t end);
    template <typename Visit> void visit_overlapping(const Aabb &bounds, Visit &&visit) const;

    std::vector<Triangle> m_triangles;
    std::vector<Vec3> m_normals;
    std::vector<std::uint32_t> m_order;
    std::vector<Node> m_nodes;
};

} // namespace mj
