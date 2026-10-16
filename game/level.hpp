#pragma once

#include "lighting.hpp"

#include <mj/collision/collision_world.hpp>
#include <mj/scene/mesh.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mj::game {

enum class ShapeKind : std::uint8_t { box, ramp, cylinder, ellipsoid, cone };

// One code-generated primitive. Cylinders use size.x as the diameter and size.y as height;
// ellipsoids fill their box; cones are described by mj::Cone.
struct Shape {
    ShapeKind kind = ShapeKind::box;
    Vec3 center;
    Vec3 size;
    Vec3 color;
    float yaw = 0;
    Surface surface;
    bool wall_jump = false; // Marked walls; floors/ceilings never grant a wall jump.
    bool solid = true;      // False: drawn only (clouds, foliage), never collided with.
    float taper = 0;        // Cones: the narrow end's diameter as a fraction of the wide end.
    bool tip_down = false;  // Cones: the narrow end points down.
    float bounce = 0;       // Launch speed of a springy top (m/s); 0 for ordinary ground.
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

// A gate to another level. Walking into it in live play travels there.
struct Portal {
    std::string name; // Shown on arrival, e.g. "To the Lantern Isles".
    Vec3 position;    // Center of the gate's opening.
    float radius = 1.2F;
    float yaw = 0;              // The way the opening faces, for drawing.
    std::string level;          // File name next to this level's file.
    std::size_t checkpoint = 0; // Arrival checkpoint in the target level.
};

// How a level looks: where its day starts and how its air and surfaces read.
struct Atmosphere {
    float time_of_day = 14.5F; // Hours; --time-of-day overrides it.
    float fog = 1;             // Scales the day/night fog density.
    bool grid = true;          // The 1 m graybox grid on level surfaces.
    bool sky_below = false;    // Floating worlds: haze below the horizon, not dark ground.
};

struct Collectible {
    std::string name;
    Vec3 position; // Fixed center; visual animation never moves the pickup trigger.
    float radius = 0.65F;
};

// Placement data loaded from a versioned JSON level file.
struct Level {
    std::string name;  // Identity: recordings store it, so renaming orphans them.
    std::string title; // Shown on arrival; defaults to the name.
    std::vector<Shape> shapes;
    std::vector<Checkpoint> checkpoints; // [0] is the spawn point.
    std::vector<LightFixture> lights;
    float kill_height = -25;
    std::vector<LevelZone> zones; // Optional named regions for diagnostics and playtest directions.
    std::vector<Collectible> collectibles; // Optional sparks, identified by their array index.
    std::vector<Portal> portals;
    Atmosphere atmosphere;
};

struct LevelGeometry {
    SceneMesh mesh;
    CollisionWorld collision; // Built from exactly the rendered triangles of solid shapes.
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
