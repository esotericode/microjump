#pragma once

#include "lighting.hpp"

#include <mj/collision/collision_world.hpp>
#include <mj/scene/mesh.hpp>

#include <cstdint>
#include <string>
#include <string_view>
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
    Surface surface;
};

struct Checkpoint {
    Vec3 position; // Feet position for respawning.
    float yaw = 0;
};

struct LevelZone {
    std::string name;
    Vec3 center;
    float radius = 20;
};

// Placement data loaded from a versioned JSON level file.
struct Level {
    std::string name;
    std::vector<Shape> shapes;
    std::vector<Checkpoint> checkpoints; // [0] is the spawn point.
    std::vector<LightFixture> lights;
    float kill_height = -25;
    std::vector<LevelZone> zones; // Optional named regions for diagnostics and playtest directions.
};

struct LevelGeometry {
    SceneMesh mesh;
    CollisionWorld collision; // Built from exactly the rendered triangles.
};

[[nodiscard]] Vec3 linear_color(unsigned int srgb);
[[nodiscard]] LevelGeometry build_geometry(const Level &level);
[[nodiscard]] SceneMesh make_player_mesh(float radius, float height);
// Small sphere drawn (tinted and emissive) at each local light.
[[nodiscard]] SceneMesh make_bulb_mesh();
// Compact test yard: slopes, stairs, step blocks, platforms, walls, a low ceiling, a sprint
// lane, and (M3) a shaded lighting pavilion with material samples plus night lamps.
// The full 200 m playground is M4.
[[nodiscard]] Level make_movement_yard();
[[nodiscard]] std::string_view zone_at(const Level &level, Vec3 position);

} // namespace mj::game
