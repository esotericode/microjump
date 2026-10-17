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
    case MoveState::double_jump:
        return "Double jump";
    case MoveState::triple_jump:
        return "Triple jump";
    case MoveState::dive:
        return "Dive";
    case MoveState::dive_slide:
        return "Belly slide";
    case MoveState::rollout:
        return "Rollout";
    case MoveState::bounce:
        return "Bounce";
    }
    return "?";
}

const char *to_string(HopTiming timing) {
    switch (timing) {
    case HopTiming::none:
        return "--";
    case HopTiming::clean:
        return "Clean";
    case HopTiming::early:
        return "Early";
    case HopTiming::late:
        return "Late";
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
    if (s.state == MoveState::dive_slide) {
        // Sliding on the belly: friction slows it, the stick only nudges the heading, and
        // once slow enough the character stands up. A jump or dive press rolls out instead.
        s.ground_speed = approach(s.ground_speed, 0, tuning.dive_slide_friction * dt);
        if (amount > 0) {
            const float turn = angle_difference(s.facing, yaw_of(direction));
            const float step = tuning.dive_slide_turn_rate * dt;
            s.facing = std::remainder(s.facing + std::clamp(turn, -step, step), k_tau);
        }
        if (s.ground_speed <= tuning.dive_getup_speed && s.ground_time >= tuning.dive_getup_time)
            s.state = s.ground_speed > 0.05F ? MoveState::run : MoveState::idle;
        return;
    }
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
    const float slope = slope_acceleration() * amount; // Positive running downhill.
    if (s.ground_speed > tuning.run_speed && amount > 0.9F && alignment > 0.9F) {
        // Carrying momentum straight ahead: downhill adds to it, uphill drains it, and flat
        // ground wears it down after the landing window. Turning or easing off the stick
        // falls through to the ordinary deceleration below, so momentum is never accidental.
        float change = slope;
        if (slope <= 0 && s.ground_time > tuning.hop_window)
            change -= tuning.overspeed_friction;
        s.ground_speed = std::max(s.ground_speed + change * dt, target);
    } else {
        const float rate_of_change =
            s.ground_speed < target ? tuning.ground_acceleration : tuning.ground_deceleration;
        s.ground_speed = approach(s.ground_speed, target, rate_of_change * dt);
        if (slope > 0 && alignment > 0.9F)
            s.ground_speed += slope * dt; // Running downhill builds speed past the run speed.
    }
    s.ground_speed = std::min(s.ground_speed, tuning.top_speed());
    s.state = MoveState::run;
}

float Player::slope_acceleration() const {
    // Gravity along the walkable surface, projected on the facing direction. The horizontal
    // part of the surface normal points downhill. Rounded edge contacts (stepping off a pad)
    // tilt the contact normal, so use the face; steep or vertical faces contribute nothing.
    const auto &s = m_current;
    const Vec3 n = s.ground_face_normal;
    if (n.y < std::cos(tuning.collision.max_slope))
        return 0;
    return tuning.slope_momentum * tuning.gravity() * n.y *
           dot(horizontal(n), yaw_forward(s.facing));
}

void Player::air_motion(Steering steering, bool jump_held, float dt) {
    const auto [direction, amount] = steering;
    auto &s = m_current;
    float scale = 1;
    const bool rising_jump = s.state == MoveState::jump || s.state == MoveState::wall_jump ||
                             s.state == MoveState::long_jump || s.state == MoveState::double_jump ||
                             s.state == MoveState::triple_jump || s.state == MoveState::rollout ||
                             s.state == MoveState::bounce;
    const bool variable = s.state == MoveState::jump || s.state == MoveState::double_jump;
    // Only ordinary and double jumps are variable; long jumps, wall kicks, triple jumps and
    // rollouts keep fixed, readable arcs. A dive stays a dive until it lands.
    if (s.velocity.y > 0 && variable && !jump_held)
        scale = tuning.release_gravity_scale;
    else if (s.velocity.y <= 0)
        scale = tuning.fall_gravity_scale;
    s.velocity.y = std::max(s.velocity.y - tuning.gravity() * scale * dt, -tuning.terminal_speed);
    if (s.velocity.y <= 0 && rising_jump)
        s.state = MoveState::fall;
    const float flying = length(horizontal(s.velocity));
    if (flying > tuning.run_speed) {
        const float kept = std::max(flying - tuning.air_drag * dt, tuning.run_speed) / flying;
        s.velocity.x *= kept;
        s.velocity.z *= kept;
    }
    if (amount <= 0 || s.recovery_timer > 0)
        return;
    const float control = s.state == MoveState::dive ? tuning.dive_air_control : 1.0F;
    // Steering adds acceleration along the input but never raises speed beyond what the
    // jump started with (or air_speed), so momentum matters and control stays limited.
    const Vec3 before = horizontal(s.velocity);
    const float limit = std::max(length(before), tuning.air_speed);
    Vec3 after = before + direction * (amount * control * tuning.air_acceleration * dt);
    if (length(after) > limit)
        after = normalized(after) * limit;
    s.velocity.x = after.x;
    s.velocity.z = after.z;
    const float turn = angle_difference(s.facing, yaw_of(direction));
    const float turn_rate = tuning.air_turn_rate * control;
    s.facing = std::remainder(s.facing + std::clamp(turn, -turn_rate * dt, turn_rate * dt), k_tau);
}

bool Player::can_dive() const {
    const auto &s = m_current;
    if (s.grounded)
        return s.state != MoveState::skid && s.state != MoveState::dive_slide &&
               s.ground_speed >= tuning.dive_min_speed;
    // Out of a jump, a fall or a kick; long jumps are committed, and one dive per airtime.
    return !s.dive_used && (s.state == MoveState::jump || s.state == MoveState::double_jump ||
                            s.state == MoveState::triple_jump || s.state == MoveState::fall ||
                            s.state == MoveState::wall_jump || s.state == MoveState::bounce);
}

void Player::dive() {
    auto &s = m_current;
    const float current = s.grounded ? s.ground_speed : length(horizontal(s.velocity));
    // A boost toward the dive's top speed, but never a loss of momentum already built.
    const float speed = std::max(
        current, std::min(current + tuning.dive_boost, std::max(tuning.dive_max_speed, current)));
    const float up = s.grounded ? tuning.dive_up_speed : s.velocity.y;
    s.velocity = yaw_forward(s.facing) * speed + Vec3{0, up, 0};
    s.grounded = false;
    s.state = MoveState::dive;
    s.dive_used = true;
    s.jump_chain = 0;
    s.buffer_timer = 0;
    s.coyote_timer = 0;
    s.recovery_timer = 0;
}

void Player::rollout() {
    // Keeps the slide's speed: diving and rolling out straight away is how the dive pays off.
    auto &s = m_current;
    s.velocity = yaw_forward(s.facing) * s.ground_speed + Vec3{0, tuning.rollout_up_speed, 0};
    s.grounded = false;
    s.state = MoveState::rollout;
    s.dive_used = true; // SM64: no diving out of a rollout.
    s.jump_chain = 0;
    s.buffer_timer = 0;
    s.coyote_timer = 0;
}

void Player::hop_timing() {
    // Called for a grounded takeoff. `lead` is how long before touchdown the press chain
    // began (negative when pressed after landing).
    auto &s = m_current;
    const float lead = s.press_age - s.ground_time;
    const float momentum = s.ground_speed - tuning.run_speed;
    if (momentum <= 0) {
        s.last_hop = HopTiming::none; // Only takeoffs carrying momentum are judged.
    } else if (lead > tuning.hop_window + 1e-4F) {
        s.last_hop = HopTiming::early;
        s.ground_speed = tuning.run_speed + momentum * tuning.early_hop_retention;
    } else {
        s.last_hop = s.ground_time > tuning.hop_window + 1e-4F ? HopTiming::late : HopTiming::clean;
    }
}

bool Player::wall_jump(bool requested, bool touching) {
    auto &s = m_current;
    if (!requested || s.grounded || s.state == MoveState::dive ||
        (!touching && s.wall_timer <= 0) ||
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
    s.jump_chain = 0;
    return true;
}

void Player::tick(const TickInput &input, const CollisionScene &world, float dt) {
    m_previous = {m_current.position, m_current.facing};
    auto &s = m_current;
    s.landed = false;
    // A moving platform carries its rider through its last step and turns them with it,
    // unless something solid is in the way. Leaving it keeps the platform's sideways and
    // rising velocity; a sinking lift never drags a jump down.
    Vec3 platform_velocity{};
    const bool riding = s.grounded && s.ground_body;
    if (riding) {
        const auto carry = world.carry(*s.ground_body, s.position);
        const auto blocked = world.sweep({capsule(), carry.offset, tuning.collision.skin});
        s.position += carry.offset * (blocked ? blocked->fraction : 1.0F);
        s.facing = std::remainder(s.facing + carry.turn, k_tau);
        platform_velocity = carry.offset * (1.0F / dt);
    }
    s.wall_timer = std::max(s.wall_timer - dt, 0.0F);
    s.recovery_timer = std::max(s.recovery_timer - dt, 0.0F);
    if (s.grounded)
        s.ground_time += dt;
    const Vec3 stick =
        yaw_right(input.camera_yaw) * input.move_x + yaw_forward(input.camera_yaw) * input.move_y;
    const float amount = std::min(length(stick), 1.0F);
    const Vec3 direction = amount > 0.02F ? normalized(stick) : Vec3{};
    const float effective = amount > 0.02F ? amount : 0.0F;

    // A press chain starts when no press is buffered; repeated presses inside the buffer
    // continue it, so mashing before a landing still counts as an early press.
    const bool pressed = input.held(input_button::jump_pressed);
    s.press_age = pressed && s.buffer_timer <= 0 ? 0.0F : s.press_age + dt;
    s.buffer_timer = pressed ? tuning.jump_buffer : std::max(s.buffer_timer - dt, 0.0F);
    s.coyote_timer = s.grounded ? tuning.coyote_time : std::max(s.coyote_timer - dt, 0.0F);

    const bool wants_jump = input.held(input_button::jump_pressed) || s.buffer_timer > 0;
    const bool wants_dive = input.held(input_button::dive_pressed);
    const bool can_jump = s.grounded || s.coyote_timer > 0;
    const bool rising = s.state == MoveState::jump || s.state == MoveState::double_jump ||
                        s.state == MoveState::triple_jump || s.state == MoveState::rollout ||
                        s.state == MoveState::dive || s.state == MoveState::bounce;
    bool consumed_jump = false;
    bool dove = false;
    if (s.grounded && s.state == MoveState::dive_slide) {
        if (wants_jump || wants_dive) {
            rollout();
            consumed_jump = true;
            dove = true; // The press is spent on the rollout.
        }
    } else if (wants_jump && can_jump && !rising) {
        consumed_jump = true;
        if (s.grounded)
            hop_timing();
        const Vec3 launch =
            s.grounded ? yaw_forward(s.facing) * s.ground_speed : horizontal(s.velocity);
        const float speed = length(launch);
        const bool long_jump = s.grounded && s.state != MoveState::skid &&
                               input.held(input_button::long_jump_held) &&
                               s.ground_speed >= tuning.long_jump_min_speed;
        // The jump chain counts only quick takeoffs from the ground after a landing.
        const bool chained = s.grounded && s.ground_time <= tuning.jump_chain_window;
        std::uint8_t stage = 1;
        if (chained && s.jump_chain == 1)
            stage = 2;
        else if (chained && s.jump_chain == 2 && speed >= tuning.triple_jump_min_speed)
            stage = 3;
        if (long_jump) {
            // A fixed boost from running speed, or more when chained from carried momentum,
            // so speed builds over several clean long jumps up to max_speed.
            const float boosted =
                std::max(std::min(speed, tuning.run_speed) * tuning.long_jump_speed_scale,
                         speed + tuning.long_jump_chain_boost);
            s.velocity = normalized(launch) * std::min(boosted, tuning.top_speed()) +
                         Vec3{0, tuning.long_jump_up_speed, 0};
        } else {
            // Height gained from speed stops at run speed: momentum buys distance, not height.
            const float scale = stage == 3   ? tuning.triple_jump_scale
                                : stage == 2 ? tuning.double_jump_scale
                                             : 1.0F;
            s.velocity =
                launch + Vec3{0,
                              (tuning.jump_velocity() +
                               tuning.jump_speed_bonus * std::min(speed, tuning.run_speed)) *
                                  scale,
                              0};
        }
        s.grounded = false;
        s.state = long_jump    ? MoveState::long_jump
                  : stage == 3 ? MoveState::triple_jump
                  : stage == 2 ? MoveState::double_jump
                               : MoveState::jump;
        // The committed long-jump arc remains closed to dives after its state becomes fall,
        // including an early descent caused by a ceiling. Landing opens diving again.
        if (long_jump)
            s.dive_used = true;
        s.jump_chain = long_jump ? 0 : stage;
        s.buffer_timer = 0;
        s.coyote_timer = 0;
    }

    consumed_jump = wall_jump(wants_jump && !consumed_jump) || consumed_jump;
    if (wants_dive && !dove && !consumed_jump && can_dive())
        dive();

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
    // Landing on a moving platform counts only the speed relative to it, so a hop in place
    // lands in place.
    Vec3 ground_velocity{};
    if (moved.grounded && moved.ground_body && !s.grounded)
        ground_velocity = world.carry(*moved.ground_body, moved.base).offset * (1.0F / dt);
    if (moved.grounded) {
        if (!s.grounded && s.state == MoveState::dive) {
            // A dive lands on its belly and slides on along the way it was flying.
            s.landed = true;
            s.ground_time = 0;
            const Vec3 flat = horizontal(s.velocity - ground_velocity);
            if (length(flat) > 0.5F)
                s.facing = yaw_of(flat);
            s.ground_speed = std::min(length(flat), tuning.top_speed());
            s.state = MoveState::dive_slide;
        } else if (!s.grounded) {
            s.landed = true;
            s.ground_time = 0;
            const Vec3 flat = horizontal(s.velocity - ground_velocity);
            const float speed = length(flat);
            if (moved.ground_body) {
                // On a mover, keep only forward speed relative to it, never turning round:
                // dropping onto a moving platform lands you standing on it.
                s.ground_speed =
                    std::clamp(dot(flat, yaw_forward(s.facing)), 0.0F, tuning.top_speed());
            } else if (speed > 0.5F) {
                // Keep momentum along the direction of travel, preferring the current facing.
                const float along = dot(flat, yaw_forward(s.facing));
                if (along > speed * 0.7F) {
                    s.ground_speed = std::min(along, tuning.top_speed());
                } else {
                    s.facing = yaw_of(flat);
                    s.ground_speed = std::min(speed, tuning.top_speed());
                }
            } else {
                s.ground_speed = 0;
            }
            s.state = s.ground_speed > 0.05F ? MoveState::run : MoveState::idle;
        }
        s.grounded = true;
        s.ground_normal = moved.ground_normal;
        s.ground_face_normal = moved.ground_face_normal;
        s.ground_body = moved.ground_body;
        s.velocity.y = 0;
    } else {
        if (s.grounded) {
            // Walked off a ledge or onto a steep slope: keep moving, start falling. A belly
            // slide off an edge stays a dive, and lands as a slide again.
            s.velocity = yaw_forward(s.facing) * s.ground_speed;
            s.dive_used = s.state == MoveState::dive_slide;
            s.state = s.state == MoveState::dive_slide ? MoveState::dive : MoveState::fall;
            s.jump_chain = 0;
        }
        s.grounded = false;
        s.ground_body.reset();
    }
    if (s.grounded && moved.ground_bounce > 0) {
        // A springy floor throws you up on a fixed arc, keeping your way and speed.
        s.velocity = yaw_forward(s.facing) * s.ground_speed + Vec3{0, moved.ground_bounce, 0};
        s.grounded = false;
        s.state = MoveState::bounce;
        s.jump_chain = 0;
        s.coyote_timer = 0;
        s.ground_body.reset();
    }
    if (riding && !s.grounded)
        s.velocity +=
            Vec3{platform_velocity.x, std::max(platform_velocity.y, 0.0F), platform_velocity.z};
    if (s.grounded) {
        s.wall_timer = 0;
        s.recovery_timer = 0;
        s.wall_used = false;
        s.dive_used = false;
    } else if (length_squared(moved.jump_wall_normal) > 0) {
        s.wall_normal = moved.jump_wall_normal;
        s.wall_timer = tuning.wall_contact_grace;
        wall_jump(wants_jump && !consumed_jump, true);
    } else if (moved.hit_wall) {
        s.wall_timer = 0; // A different, unmarked face cannot borrow the old wall's window.
    }
}

} // namespace mj::game
