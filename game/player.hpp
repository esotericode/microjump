#pragma once

#include <mj/collision/character_mover.hpp>
#include <mj/core/input_record.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace mj::game {

enum class MoveState : std::uint8_t {
    idle,
    run,
    skid,
    jump,
    wall_jump,
    long_jump,
    fall,
    double_jump,
    triple_jump,
    dive,       // Airborne lunge, from a run or out of a jump.
    dive_slide, // Grounded belly slide after a dive lands.
    rollout,    // The hop out of a belly slide.
    bounce,     // Launched by a springy floor (a bounce mushroom).
};
[[nodiscard]] const char *to_string(MoveState state);

// How the last grounded takeoff was timed relative to its landing (see docs/movement.md).
enum class HopTiming : std::uint8_t { none, clean, early, late };
[[nodiscard]] const char *to_string(HopTiming timing);

// Every movement value in one place. Units: meters, seconds, radians. Edited live in the
// tuning window (F2); see docs/movement.md for what each value does.
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
    float jump_speed_bonus = 0.12F;     // Launch speed per m/s of ground speed, up to run speed.
    float release_gravity_scale = 2.8F; // Ordinary jumps only, while rising after a release.
    float fall_gravity_scale = 1.45F;
    float terminal_speed = 32.0F;
    float air_acceleration = 9.0F;
    float air_speed = 7.0F; // Steering can always reach this; faster momentum is kept.
    float air_turn_rate = 5.0F;
    float coyote_time = 0.1F;
    float jump_buffer = 0.12F;
    // Momentum: ground speed above run_speed. Built by long jumps and downhill running, kept
    // by clean hops, lost to friction, uphill running, sharp turns, braking and air drag.
    float max_speed = 22.0F;
    float hop_window = 0.075F;        // A takeoff this close to landing (either side) is clean.
    float early_hop_retention = 0.4F; // Momentum kept by a jump pressed before that window.
    float overspeed_friction = 14.0F; // Momentum lost per second on flat ground.
    float slope_momentum = 0.4F;      // Fraction of slope gravity that adds to/drains momentum.
    float air_drag = 0.6F;            // Momentum lost per second in the air.
    // M4 moves. Long jump needs grounded speed plus the modifier at takeoff.
    float long_jump_min_speed = 6.0F;
    float long_jump_speed_scale = 1.8F; // Launch from up to run speed...
    float long_jump_chain_boost = 2.5F; // ...or current speed plus this, whichever is larger.
    float long_jump_up_speed = 8.5F;
    float wall_jump_speed = 8.5F;
    float wall_jump_up_speed = 12.0F;
    float wall_contact_grace = 0.1F;
    float wall_recovery_time = 0.14F; // Brief steering lock guarantees separation.
    // Jump chain, after Super Mario 64: jump again within the window after landing a jump for
    // a double jump; again after that, while moving, for a triple jump (a fixed arc).
    float jump_chain_window = 0.15F;    // SM64: 5 frames at 30 Hz.
    float double_jump_scale = 1.2F;     // Launch speed relative to a jump (SM64: 52 + 8 / 42 + 8).
    float triple_jump_scale = 1.38F;    // SM64: 69 / 50.
    float triple_jump_min_speed = 5.6F; // SM64: forward speed above 20 of 32.
    // Dive: a lunge that adds speed, then a belly slide you can roll out of.
    float dive_min_speed = 6.0F;   // Ground speed to dive from a run; any speed from a jump.
    float dive_boost = 4.2F;       // Added to forward speed (SM64: 15 of 32)...
    float dive_max_speed = 13.5F;  // ...up to this (SM64: 48); faster momentum is kept.
    float dive_up_speed = 3.0F;    // Lift when diving from the ground.
    float dive_air_control = 0.3F; // Fraction of normal air steering while diving.
    float dive_slide_friction = 9.0F;
    float dive_slide_turn_rate = 1.5F;
    float dive_getup_time = 0.3F;  // Shortest belly slide before standing up...
    float dive_getup_speed = 1.2F; // ...once it has slowed below this.
    float rollout_up_speed = 7.5F; // SM64: 30 of a 50 jump.
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
    [[nodiscard]] float top_speed() const { // Never below run speed, whatever the sliders say.
        return std::max(max_speed, run_speed);
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
    Vec3 ground_face_normal{0, 1, 0}; // Surface slope, unaffected by rounded edge contacts.
    float coyote_timer = 0;
    float buffer_timer = 0;
    Vec3 wall_normal;
    Vec3 last_kick_normal;
    float wall_timer = 0;
    float recovery_timer = 0;
    bool wall_used = false; // Same-facing walls cannot be farmed before landing.
    bool landed = false;    // Became grounded this tick.
    float ground_time = 0;  // Since the last landing.
    float press_age = 0;    // Since the first press of the current buffered press chain.
    HopTiming last_hop = HopTiming::none;
    std::uint8_t jump_chain = 0; // The last takeoff: 1 jump, 2 double, 3 triple, 0 anything else.
    bool dive_used = false;      // One dive per airtime.
    std::optional<std::uint32_t> ground_body; // The moving platform stood on, if any.
};

struct PlayerPose {
    Vec3 position;
    float facing = 0;
};

class Player {
  public:
    PlayerTuning tuning;

    void place(Vec3 position, float facing);
    // A platform stood on carries the player through its last step before the move.
    void tick(const TickInput &input, const CollisionScene &world, float dt);
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
    [[nodiscard]] float slope_acceleration() const;
    void air_motion(Steering steering, bool jump_held, float dt);
    void hop_timing();
    bool wall_jump(bool requested, bool touching = false);
    [[nodiscard]] bool can_dive() const;
    void dive();
    void rollout();

    PlayerState m_current;
    PlayerPose m_previous;
};

} // namespace mj::game
