#pragma once

#include <mj/collision/collision_world.hpp>

namespace mj {

struct MoveSettings {
    float max_slope = 0.872665F; // 50 degrees: steeper surfaces behave like walls.
    float step_height = 0.35F;   // Ledges up to this height are climbed without jumping.
    float ground_snap = 0.3F;    // Grounded movement follows descending ground this far.
    float skin = 0.01F;
    int max_slides = 4;
};

struct MoveRequest {
    Capsule shape;
    Vec3 displacement;
    bool grounded = false; // Grounded requests step up, follow slopes and snap down.
};

struct MoveResult {
    Vec3 base;
    bool grounded = false;
    Vec3 ground_normal{0, 1, 0};
    Vec3 ground_face_normal{0, 1, 0}; // Triangle normal; edge contacts tilt ground_normal.
    float ground_bounce = 0;          // The ground's authored launch speed, if springy.
    bool hit_wall = false;
    Vec3 wall_normal;
    Vec3 jump_wall_normal; // Nonzero only for a designated, near-vertical face contact.
    bool hit_ceiling = false;
    bool stepped = false;
};

[[nodiscard]] inline bool is_walkable(Vec3 normal, const MoveSettings &settings) {
    return normal.y >= std::cos(settings.max_slope);
}

// Kinematic collide-and-slide for an upright capsule against static world triangles.
[[nodiscard]] MoveResult move_character(const CollisionWorld &world, const MoveSettings &settings,
                                        const MoveRequest &request);

} // namespace mj
