#include "follow_camera.hpp"

namespace mj::game {
namespace {
float smoothing(float half_life, float dt) {
    return half_life <= 0 ? 1.0F : 1.0F - std::exp2(-dt / half_life);
}
} // namespace

void FollowCamera::snap(const PlayerState &player, float yaw) {
    m_yaw = yaw;
    m_pitch = tuning.default_pitch;
    m_focus = player.position + Vec3{0, tuning.focus_height, 0};
    m_previous_focus = m_focus;
    m_distance = tuning.distance;
    m_previous_distance = m_distance;
}

void FollowCamera::look(const LookInput &input) {
    const float invert = tuning.invert_y ? -1.0F : 1.0F;
    const float yaw = input.mouse_dx * tuning.mouse_sensitivity +
                      input.stick_x * tuning.stick_yaw_speed * input.frame_seconds;
    const float pitch = -(input.mouse_dy * tuning.mouse_sensitivity +
                          input.stick_y * tuning.stick_pitch_speed * input.frame_seconds) *
                        invert;
    set_orientation({m_yaw + yaw, m_pitch + pitch});
}

void FollowCamera::set_orientation(Orientation orientation) {
    m_yaw = std::remainder(orientation.yaw, k_tau);
    m_pitch = std::clamp(orientation.pitch, tuning.min_pitch, tuning.max_pitch);
}

float FollowCamera::free_distance(Vec3 focus, const CollisionScene &world) const {
    const float desired = std::max(tuning.distance, tuning.min_distance);
    const Vec3 back = -view_forward(m_yaw, m_pitch) * desired;
    const Capsule probe{focus - Vec3{0, tuning.probe_radius, 0}, tuning.probe_radius,
                        tuning.probe_radius * 2};
    const auto hit = world.sweep({probe, back, 0.02F});
    // The preferred minimum boom length must never push the probe through a nearby wall.
    return hit ? desired * hit->fraction : desired;
}

void FollowCamera::tick(const PlayerState &player, const CollisionScene &world, float dt) {
    m_previous_focus = m_focus;
    m_previous_distance = m_distance;
    const Vec3 target = player.position + Vec3{0, tuning.focus_height, 0};
    const float follow = smoothing(tuning.follow_half_life, dt);
    m_focus.x += (target.x - m_focus.x) * follow;
    m_focus.z += (target.z - m_focus.z) * follow;
    const float offset = target.y - m_focus.y;
    const bool outside_window = std::abs(offset) > tuning.air_window;
    const float vertical = smoothing(
        player.grounded || outside_window ? tuning.vertical_half_life : tuning.air_half_life, dt);
    m_focus.y += offset * vertical;
    if (outside_window) // Never let a long fall leave the player out of frame.
        m_focus.y = target.y - std::copysign(tuning.air_window, offset);

    const float free = free_distance(m_focus, world);
    m_distance = free < m_distance
                     ? free
                     : m_distance + (free - m_distance) * smoothing(tuning.recover_half_life, dt);
}

CameraPose FollowCamera::pose(float alpha, const CollisionScene &world) const {
    const Vec3 focus = lerp(m_previous_focus, m_focus, alpha);
    const float smoothed = m_previous_distance + (m_distance - m_previous_distance) * alpha;
    const float distance = std::min(smoothed, free_distance(focus, world));
    return {focus - view_forward(m_yaw, m_pitch) * distance, m_yaw, m_pitch};
}

} // namespace mj::game
