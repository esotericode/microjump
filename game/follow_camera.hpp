#pragma once

#include "player.hpp"

#include <mj/collision/collision_world.hpp>
#include <mj/scene/debug_camera.hpp>

namespace mj::game {

struct FollowCameraTuning {
    float distance = 7.0F;
    float focus_height = 1.3F;         // Above the feet.
    float mouse_sensitivity = 0.0025F; // Radians per mouse count.
    float stick_yaw_speed = 3.2F;      // Radians per second at full tilt.
    float stick_pitch_speed = 2.2F;
    bool invert_y = false;
    float min_pitch = -1.25F; // Looking down.
    float max_pitch = 0.55F;  // Looking up.
    float default_pitch = -0.32F;
    float follow_half_life = 0.04F;   // Horizontal focus lag.
    float vertical_half_life = 0.12F; // Vertical focus lag while grounded.
    float air_half_life = 0.6F;       // Vertical lag while airborne keeps jumps steady...
    float air_window = 1.8F;          // ...until the player drifts this far from the focus.
    float probe_radius = 0.25F;
    float min_distance = 0.9F;
    float recover_half_life = 0.3F; // Easing back out after an obstruction clears.
};

struct LookInput {
    float mouse_dx = 0; // Relative mouse counts.
    float mouse_dy = 0;
    float stick_x = 0; // Right stick, -1..1.
    float stick_y = 0;
    float frame_seconds = 0;
};
struct Orientation {
    float yaw = 0;
    float pitch = 0;
};

// Third-person orbit camera. Orientation changes immediately on input; focus smoothing and
// obstruction recovery run on the fixed tick, so they are independent of the render rate.
class FollowCamera {
  public:
    FollowCameraTuning tuning;

    void snap(const PlayerState &player, float yaw);
    void look(const LookInput &input);
    void set_orientation(Orientation orientation);
    void tick(const PlayerState &player, const CollisionWorld &world, float dt);
    // Render pose; the obstruction is re-checked for the current orientation every frame.
    [[nodiscard]] CameraPose pose(float alpha, const CollisionWorld &world) const;
    [[nodiscard]] float yaw() const {
        return m_yaw;
    }
    [[nodiscard]] float pitch() const {
        return m_pitch;
    }
    [[nodiscard]] Vec3 focus() const {
        return m_focus;
    }
    [[nodiscard]] float distance() const {
        return m_distance;
    }

  private:
    [[nodiscard]] float free_distance(Vec3 focus, const CollisionWorld &world) const;

    float m_yaw = 0;
    float m_pitch = -0.32F;
    Vec3 m_focus;
    Vec3 m_previous_focus;
    float m_distance = 7;
    float m_previous_distance = 7;
};

} // namespace mj::game
