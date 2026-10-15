#pragma once

#include <mj/collision/collision_world.hpp>
#include <mj/scene/mesh.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace mj::game {

enum class ShapeKind : std::uint8_t { box, ramp, cylinder };

// One code-generated primitive. Cylinders use size.x as the diameter and size.y as height.
struct Shape {
    ShapeKind kind = ShapeKind::box;
    Vec3 center;
    Vec3 size;
    Vec3 color;
    float yaw = 0;
};

struct Checkpoint {
    Vec3 position; // Feet position for respawning.
    float yaw = 0;
};

// Placement data only; M4 loads the same structure from a versioned JSON level file.
struct Level {
    std::string name;
    std::vector<Shape> shapes;
    std::vector<Checkpoint> checkpoints; // [0] is the spawn point.
    float kill_height = -25;
};

struct LevelGeometry {
    SceneMesh mesh;
    CollisionWorld collision; // Built from exactly the rendered triangles.
};

[[nodiscard]] Vec3 linear_color(unsigned int srgb);
[[nodiscard]] LevelGeometry build_geometry(const Level &level);
[[nodiscard]] SceneMesh make_player_mesh(float radius, float height);
// Compact M2 test yard: slopes, stairs, step blocks, platforms, walls, a low ceiling and a
// sprint lane. The full 200 m playground is M4.
[[nodiscard]] Level make_movement_yard();

} // namespace mj::game
