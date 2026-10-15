#pragma once

#include <mj/collision/character_mover.hpp>
#include <mj/core/input_record.hpp>

#include <cstdint>

namespace mj::game {

enum class MoveState : std::uint8_t { idle, run, skid, jump, fall };
[[nodiscard]] const char *to_string(MoveState state);

// Every movement value in one place. Units: meters, seconds, radians. Edited live in the
// tuning window (F2); see docs/m2-movement.md for what each value does.
struct PlayerTuning {
    // Ground.
    float run_speed = 9.0F;
    float walk_fraction = 0.4F; // Stick scale while the walk modifier is held.
    float ground_acceleration = 20.0F;
    float ground_deceleration = 30.0F;
    float turn_rate_slow = 18.0F; // Facing turn rate at standstill...
    float turn_rate_fast = 6.0F;  // ...and at run speed.
    float turn_slowdown = 0.35F;  // Target speed lost while facing away from the input.
    float skid_angle = 2.3F;      // Reversing beyond this angle at speed starts a skid.
    float skid_min_speed = 4.5F;
    float skid_deceleration = 40.0F;
    // Jump and air.
    float jump_height = 2.2F; // Apex from standstill with the button held.
    float jump_time_to_apex = 0.38F;
    float jump_speed_bonus = 0.12F;     // Launch speed added per m/s of ground speed.
    float release_gravity_scale = 2.8F; // While rising after the button is released.
    float fall_gravity_scale = 1.45F;
    float terminal_speed = 32.0F;
    float air_acceleration = 9.0F;
    float air_speed = 7.0F; // Steering can always reach this; faster momentum is kept.
    float air_turn_rate = 5.0F;
    float coyote_time = 0.1F;
    float jump_buffer = 0.12F;
    // Body and collision.
    float radius = 0.35F;
    float height = 1.6F;
    MoveSettings collision;

    [[nodiscard]] float gravity() const {
        return 2 * jump_height / (jump_time_to_apex * jump_time_to_apex);
    }
    [[nodiscard]] float jump_velocity() const {
        return 2 * jump_height / jump_time_to_apex;
    }
};

struct PlayerState {
    Vec3 position; // Capsule base (feet).
    Vec3 velocity;
    float facing = 0;       // Yaw; 0 faces -Z.
    float ground_speed = 0; // Along `facing` while grounded.
    MoveState state = MoveState::idle;
    bool grounded = true;
    Vec3 ground_normal{0, 1, 0};
    float coyote_timer = 0;
    float buffer_timer = 0;
    bool landed = false; // Became grounded this tick.
};

struct PlayerPose {
    Vec3 position;
    float facing = 0;
};

class Player {
  public:
    PlayerTuning tuning;

    void place(Vec3 position, float facing);
    void tick(const TickInput &input, const CollisionWorld &world, float dt);
    [[nodiscard]] const PlayerState &current() const {
        return m_current;
    }
    [[nodiscard]] PlayerPose interpolated(float alpha) const;
    [[nodiscard]] Capsule capsule() const {
        return {m_current.position, tuning.radius, tuning.height};
    }

  private:
    // Camera-relative stick resolved to a world direction and a 0..1 magnitude.
    struct Steering {
        Vec3 direction;
        float amount = 0;
    };
    void ground_motion(Steering steering, float dt);
    void air_motion(Steering steering, bool jump_held, float dt);

    PlayerState m_current;
    PlayerPose m_previous;
};

} // namespace mj::game
