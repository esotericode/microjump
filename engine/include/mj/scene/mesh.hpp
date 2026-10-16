#pragma once

#include <mj/collision/geometry.hpp>
#include <mj/math/math.hpp>

#include <vector>

namespace mj {

// Material response for the lighting model (linear 0..1).
struct Surface {
    float roughness = 0.7F;
    float metallic = 0;
};

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color; // Linear albedo.
    Surface surface;
};

struct Box {
    Vec3 center;
    Vec3 size;
    Vec3 color;
    float yaw = 0;
};
// Wedge filling its box; the sloped face rises toward local -Z (yaw 0: forward).
struct Ramp {
    Vec3 center;
    Vec3 size;
    Vec3 color;
    float yaw = 0;
};
struct CapsuleMesh {
    Vec3 base; // Lowest point.
    float radius;
    float height;
    Vec3 color;
};
struct SceneMesh {
    std::vector<Vertex> vertices;
    Surface surface; // Applied to every vertex added after it is set.
    void add_box(const Box &box);
    void add_ramp(const Ramp &ramp);
    void add_cylinder(Vec3 center, float radius, float height, Vec3 color);
    void add_capsule(const CapsuleMesh &capsule);
    // Every three vertices form one outward-facing triangle; collision uses the same surfaces.
    [[nodiscard]] std::vector<Triangle> triangles() const;

  private:
    void emit(Vec3 position, Vec3 normal, Vec3 color);
};

} // namespace mj
