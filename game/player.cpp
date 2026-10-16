#include "player.hpp"

namespace mj::game {

const char *to_string(MoveState state) {
    switch (state) {
    case MoveState::idle:
        return "Idle";
    case MoveState::run:
        return "Run";
    case MoveState::skid:
        return "Skid";
    case MoveState::jump:
        return "Jump";
    case MoveState::wall_jump:
        return "Wall jump";
    case MoveState::long_jump:
        return "Long jump";
    case MoveState::fall:
        return "Fall";
    }
    return "?";
}

void Player::place(Vec3 position, float facing) {
    m_current = {};
    m_current.position = position;
    m_current.facing = facing;
    m_current.grounded = false; // The first tick finds the ground, if any.
    m_current.state = MoveState::fall;
    m_previous = {position, facing};
}

PlayerPose Player::interpolated(float alpha) const {
    return {lerp(m_previous.position, m_current.position, alpha),
            lerp_angle(m_previous.facing, m_current.facing, alpha)};
}

void Player::ground_motion(Steering steering, float dt) {
    const auto [direction, amount] = steering;
    auto &s = m_current;
    if (s.state == MoveState::skid) {
        s.ground_speed = approach(s.ground_speed, 0, tuning.skid_deceleration * dt);
        if (s.ground_speed > 0.3F)
            return;
        // Turnaround complete: face the input and accelerate from rest.
        if (amount > 0)
            s.facing = yaw_of(direction);
        s.state = MoveState::run;
        return;
    }
    if (amount <= 0) {
        s.ground_speed = approach(s.ground_speed, 0, tuning.ground_deceleration * dt);
        s.state = s.ground_speed > 0.05F ? MoveState::run : MoveState::idle;
        return;
    }
    const float turn = angle_difference(s.facing, yaw_of(direction));
    if (std::abs(turn) > tuning.skid_angle && s.ground_speed > tuning.skid_min_speed) {
        s.state = MoveState::skid;
        return;
    }
    const float speed_fraction = std::clamp(s.ground_speed / tuning.run_speed, 0.0F, 1.0F);
    const float rate =
        tuning.turn_rate_slow + (tuning.turn_rate_fast - tuning.turn_rate_slow) * speed_fraction;
    s.facing += std::clamp(turn, -rate * dt, rate * dt);
    s.facing = std::remainder(s.facing, k_tau);
    const float alignment = std::max(std::cos(turn), 0.0F);
    const float target = amount * tuning.run_speed * (1 - tuning.turn_slowdown * (1 - alignment));
    const float rate_of_change =
        s.ground_speed < target ? tuning.ground_acceleration : tuning.ground_deceleration;
    s.ground_speed = approach(s.ground_speed, target, rate_of_change * dt);
    s.state = MoveState::run;
}

void Player::air_motion(Steering steering, bool jump_held, float dt) {
    const auto [direction, amount] = steering;
    auto &s = m_current;
    float scale = 1;
    const bool rising_jump = s.state == MoveState::jump || s.state == MoveState::wall_jump ||
                             s.state == MoveState::long_jump;
    if (s.velocity.y > 0 && rising_jump && !jump_held)
        scale = tuning.release_gravity_scale;
    else if (s.velocity.y <= 0)
        scale = tuning.fall_gravity_scale;
    s.velocity.y = std::max(s.velocity.y - tuning.gravity() * scale * dt, -tuning.terminal_speed);
    if (s.velocity.y <= 0 && rising_jump)
        s.state = MoveState::fall;
    if (amount <= 0 || s.recovery_timer > 0)
        return;
    // Steering adds acceleration along the input but never raises speed beyond what the
    // jump started with (or air_speed), so momentum matters and control stays limited.
    const Vec3 before = horizontal(s.velocity);
    const float limit = std::max(length(before), tuning.air_speed);
    Vec3 after = before + direction * (amount * tuning.air_acceleration * dt);
    if (length(after) > limit)
        after = normalized(after) * limit;
    s.velocity.x = after.x;
    s.velocity.z = after.z;
    const float turn = angle_difference(s.facing, yaw_of(direction));
    s.facing = std::remainder(
        s.facing + std::clamp(turn, -tuning.air_turn_rate * dt, tuning.air_turn_rate * dt), k_tau);
}

bool Player::wall_jump(bool requested, bool touching) {
    auto &s = m_current;
    if (!requested || s.grounded || (!touching && s.wall_timer <= 0) ||
        (s.wall_used && dot(s.wall_normal, s.last_kick_normal) > 0.5F))
        return false;
    // A repeatable outward angle; normal air steering resumes after a short separation.
    s.velocity = s.wall_normal * tuning.wall_jump_speed + Vec3{0, tuning.wall_jump_up_speed, 0};
    s.facing = yaw_of(s.wall_normal);
    s.state = MoveState::wall_jump;
    s.last_kick_normal = s.wall_normal;
    s.wall_used = true;
    s.recovery_timer = tuning.wall_recovery_time;
    s.wall_timer = 0;
    s.buffer_timer = 0;
    s.coyote_timer = 0;
    return true;
}

void Player::tick(const TickInput &input, const CollisionWorld &world, float dt) {
    m_previous = {m_current.position, m_current.facing};
    auto &s = m_current;
    s.landed = false;
    s.wall_timer = std::max(s.wall_timer - dt, 0.0F);
    s.recovery_timer = std::max(s.recovery_timer - dt, 0.0F);
    const Vec3 stick =
        yaw_right(input.camera_yaw) * input.move_x + yaw_forward(input.camera_yaw) * input.move_y;
    const float amount = std::min(length(stick), 1.0F);
    const Vec3 direction = amount > 0.02F ? normalized(stick) : Vec3{};
    const float effective = amount > 0.02F ? amount : 0.0F;

    s.buffer_timer = input.held(input_button::jump_pressed) ? tuning.jump_buffer
                                                            : std::max(s.buffer_timer - dt, 0.0F);
    s.coyote_timer = s.grounded ? tuning.coyote_time : std::max(s.coyote_timer - dt, 0.0F);

    const bool wants_jump = input.held(input_button::jump_pressed) || s.buffer_timer > 0;
    const bool can_jump = s.grounded || s.coyote_timer > 0;
    bool consumed_jump = false;
    if (wants_jump && can_jump && s.state != MoveState::jump) {
        consumed_jump = true;
        const Vec3 launch =
            s.grounded ? yaw_forward(s.facing) * s.ground_speed : horizontal(s.velocity);
        const bool long_jump = s.grounded && s.state != MoveState::skid &&
                               input.held(input_button::long_jump_held) &&
                               s.ground_speed >= tuning.long_jump_min_speed;
        s.velocity =
            long_jump
                ? normalized(launch) * (std::min(length(launch), tuning.run_speed) *
                                        tuning.long_jump_speed_scale) +
                      Vec3{0, tuning.long_jump_up_speed, 0}
                : launch +
                      Vec3{0, tuning.jump_velocity() + tuning.jump_speed_bonus * length(launch), 0};
        s.grounded = false;
        s.state = long_jump ? MoveState::long_jump : MoveState::jump;
        s.buffer_timer = 0;
        s.coyote_timer = 0;
    }

    consumed_jump = wall_jump(wants_jump && !consumed_jump) || consumed_jump;

    if (s.grounded) {
        ground_motion({direction, effective}, dt);
        s.velocity = yaw_forward(s.facing) * s.ground_speed;
    } else {
        air_motion({direction, effective}, input.held(input_button::jump_held), dt);
    }

    const Vec3 before = s.position;
    const auto moved =
        move_character(world, tuning.collision,
                       {{s.position, tuning.radius, tuning.height}, s.velocity * dt, s.grounded});
    s.position = moved.base;
    const Vec3 actual = horizontal(moved.base - before) * (1.0F / dt);

    if (moved.hit_ceiling && s.velocity.y > 0)
        s.velocity.y = 0;
    if (!s.grounded &&
        s.velocity.y < 0) // Sliding down a steep face falls only as fast as it moves.
        s.velocity.y = std::max(s.velocity.y, (moved.base.y - before.y) / dt);
    if (moved.hit_wall) {
        const Vec3 wall = normalized(horizontal(moved.wall_normal));
        if (s.grounded) {
            // Head-on stops; glancing contact slides along the wall without bleeding speed.
            if (dot(yaw_forward(s.facing), wall) < -0.85F)
                s.ground_speed = std::min(s.ground_speed, length(actual));
        } else {
            const float into = dot(s.velocity, wall);
            if (into < 0)
                s.velocity -= wall * into;
        }
    }
    if (moved.grounded) {
        if (!s.grounded) {
            s.landed = true;
            const Vec3 flat = horizontal(s.velocity);
            const float speed = length(flat);
            if (speed > 0.5F) {
                // Keep momentum along the direction of travel, preferring the current facing.
                const float along = dot(flat, yaw_forward(s.facing));
                if (along > speed * 0.7F) {
                    s.ground_speed = along;
                } else {
                    s.facing = yaw_of(flat);
                    s.ground_speed = speed;
                }
            } else {
                s.ground_speed = 0;
            }
            s.state = s.ground_speed > 0.05F ? MoveState::run : MoveState::idle;
        }
        s.grounded = true;
        s.ground_normal = moved.ground_normal;
        s.velocity.y = 0;
    } else {
        if (s.grounded) {
            // Walked off a ledge or onto a steep slope: keep moving, start falling.
            s.velocity = yaw_forward(s.facing) * s.ground_speed;
            s.state = MoveState::fall;
        }
        s.grounded = false;
    }
    if (s.grounded) {
        s.wall_timer = 0;
        s.recovery_timer = 0;
        s.wall_used = false;
    } else if (length_squared(moved.jump_wall_normal) > 0) {
        s.wall_normal = moved.jump_wall_normal;
        s.wall_timer = tuning.wall_contact_grace;
        wall_jump(wants_jump && !consumed_jump, true);
    } else if (moved.hit_wall) {
        s.wall_timer = 0; // A different, unmarked face cannot borrow the old wall's window.
    }
}

} // namespace mj::game
