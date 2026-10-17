#pragma once

#include <mj/collision/geometry.hpp>

#include <cmath>
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
    std::optional<std::uint32_t> body; // The moving body hit; empty for static geometry.
};
struct RayHit {
    float distance = 0;
    Vec3 normal; // Faces the ray origin.
    Vec3 point;
    std::uint32_t triangle = 0;
    std::optional<std::uint32_t> body;
};
struct Penetration {
    Vec3 push;       // Moves the capsule out of its deepest contact...
    float depth = 0; // ...by this much; zero when nothing overlaps.
};

// Where a moving body is: a translation plus a turn about the vertical axis (yaw, as in
// yaw_translation). Bodies never tilt, so an upright capsule stays upright in body space.
struct BodyPose {
    Vec3 position;
    float yaw = 0;
};
[[nodiscard]] inline Vec3 rotate_yaw(Vec3 v, float yaw) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}
[[nodiscard]] inline Vec3 to_world(const BodyPose &pose, Vec3 local) {
    return pose.position + rotate_yaw(local, pose.yaw);
}
[[nodiscard]] inline Vec3 to_local(const BodyPose &pose, Vec3 world) {
    return rotate_yaw(world - pose.position, -pose.yaw);
}

// Static world geometry: a triangle soup with a bounding volume hierarchy.
// Queries are one-sided: triangles face outward (counter-clockwise winding) and only
// block motion entering their front side, which matches closed generated primitives.
// The world can also hold moving bodies, each with its own triangles in body space. Its
// own queries see only the static triangles; a CollisionScene adds the bodies at a pose.
class CollisionWorld {
  public:
    CollisionWorld() = default;
    explicit CollisionWorld(std::span<const Triangle> triangles);

    [[nodiscard]] std::optional<RayHit> raycast(const Ray &ray) const;
    [[nodiscard]] std::optional<SweepHit> sweep(const SweepQuery &query) const;
    // Offset that moves an overlapping capsule at least `skin` / 2 away from all surfaces.
    [[nodiscard]] Vec3 depenetration(const Capsule &shape, float skin) const;
    // One step of depenetration: the push out of the single deepest contact.
    [[nodiscard]] Penetration deepest_penetration(const Capsule &shape, float skin) const;
    [[nodiscard]] std::span<const Triangle> triangles() const {
        return m_triangles;
    }

    // Adds a moving body: triangles in body space, and the pose it rests at when a scene
    // supplies none. Returns the body's index.
    std::uint32_t add_body(std::span<const Triangle> local, BodyPose rest);
    [[nodiscard]] std::size_t body_count() const {
        return m_bodies.size();
    }
    [[nodiscard]] const CollisionWorld &body(std::uint32_t index) const {
        return m_bodies[index];
    }
    [[nodiscard]] BodyPose rest_pose(std::uint32_t index) const {
        return m_rest_poses[index];
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
    std::vector<CollisionWorld> m_bodies;
    std::vector<BodyPose> m_rest_poses;
};

// How a point riding a body moved during the body's last step.
struct BodyCarry {
    Vec3 offset;
    float turn = 0; // Yaw change, to turn a rider with a turntable.
};

// Where each body is now and where it was a step ago (for carrying riders): one pose per
// body, or empty spans for rest poses.
struct BodyPoses {
    std::span<const BodyPose> now;
    std::span<const BodyPose> before;
};

// The static world plus its moving bodies, each at a pose. Queries report the body they
// hit. A plain CollisionWorld converts to a scene with every body at its rest pose.
class CollisionScene {
  public:
    // Implicit: every static world is a still scene.
    CollisionScene(const CollisionWorld &world) : m_world(&world) {}
    CollisionScene(const CollisionWorld &world, BodyPoses poses)
        : m_world(&world), m_poses(poses.now), m_previous(poses.before) {}

    [[nodiscard]] std::optional<RayHit> raycast(const Ray &ray) const;
    [[nodiscard]] std::optional<SweepHit> sweep(const SweepQuery &query) const;
    [[nodiscard]] Vec3 depenetration(const Capsule &shape, float skin) const;
    // The triangle (in its own body's space) that a hit refers to, for its surface flags.
    [[nodiscard]] const Triangle &triangle(std::optional<std::uint32_t> body,
                                           std::uint32_t index) const;
    [[nodiscard]] BodyPose pose(std::uint32_t body) const;
    [[nodiscard]] BodyCarry carry(std::uint32_t body, Vec3 point) const;
    [[nodiscard]] const CollisionWorld &world() const {
        return *m_world;
    }

  private:
    const CollisionWorld *m_world;
    std::span<const BodyPose> m_poses;
    std::span<const BodyPose> m_previous;
};

} // namespace mj
