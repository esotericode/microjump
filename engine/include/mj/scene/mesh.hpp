#pragma once

#include <mj/math/math.hpp>

#include <vector>

namespace mj {

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;
};

struct Box {
    Vec3 center;
    Vec3 size;
    Vec3 color;
    float yaw = 0;
};
struct SceneMesh {
    std::vector<Vertex> vertices;
    void add_box(const Box &box);
    void add_cylinder(Vec3 center, float radius, float height, Vec3 color);
};

} // namespace mj
