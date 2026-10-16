#include "character.hpp"

#include "level.hpp"

#include <algorithm>
#include <cmath>

namespace mj::game {
namespace {

// Character space: feet at the origin, +Y up, -Z forward, +X right (as yaw_translation maps).
constexpr float k_run_speed = 9.0F;    // Speed the run cycle and lean treat as a full run.
constexpr float k_teleport = 2.0F;     // Farther than any tick can move: a respawn.
constexpr float k_clock_wrap = 118.4F; // Whole blink (3.7 s) and breath (3.2 s) periods.

float smooth(float value, float target, float rate, float dt) {
    return value + (target - value) * (1 - std::exp(-rate * dt));
}

Mat4 identity() {
    Mat4 result;
    for (std::size_t i = 0; i < 4; ++i)
        result.at(i, i) = 1;
    return result;
}

Mat4 translation(Vec3 offset) {
    Mat4 result = identity();
    result.at(0, 3) = offset.x;
    result.at(1, 3) = offset.y;
    result.at(2, 3) = offset.z;
    return result;
}

Mat4 scaling(Vec3 scale) {
    Mat4 result = identity();
    result.at(0, 0) = scale.x;
    result.at(1, 1) = scale.y;
    result.at(2, 2) = scale.z;
    return result;
}

// Columns are the images of local X, Y and Z.
Mat4 basis(Vec3 x, Vec3 y, Vec3 z) {
    Mat4 result = identity();
    for (std::size_t column = 0; column < 3; ++column) {
        const Vec3 axis = std::array{x, y, z}[column];
        result.at(0, column) = axis.x;
        result.at(1, column) = axis.y;
        result.at(2, column) = axis.z;
    }
    return result;
}

// Positive pitch tips +Y toward +Z (backward); positive roll tips +Y toward -X (left).
Mat4 rotation_x(float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return basis({1, 0, 0}, {0, c, s}, {0, -s, c});
}
Mat4 rotation_y(float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return basis({c, 0, -s}, {0, 1, 0}, {s, 0, c});
}
Mat4 rotation_z(float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return basis({c, s, 0}, {-s, c, 0}, {0, 0, 1});
}

CharacterAnimator::Pose blend(const CharacterAnimator::Pose &a, const CharacterAnimator::Pose &b,
                              float t) {
    const auto mix = [t](float from, float to) { return from + (to - from) * t; };
    float phase = a.run_phase + std::remainder(b.run_phase - a.run_phase, 1.0F) * t;
    phase -= std::floor(phase);
    return {.run_phase = phase,
            .stride = mix(a.stride, b.stride),
            .squash = mix(a.squash, b.squash),
            .lean = mix(a.lean, b.lean),
            .bank = mix(a.bank, b.bank),
            .air = mix(a.air, b.air),
            .dive = mix(a.dive, b.dive),
            .skid = mix(a.skid, b.skid),
            .spin = mix(a.spin, b.spin),
            .rise = mix(a.rise, b.rise),
            .blink = mix(a.blink, b.blink),
            .breath = mix(a.breath, b.breath),
            .glow = mix(a.glow, b.glow),
            .antenna = lerp(a.antenna, b.antenna, t)};
}

struct Palette {
    Vec3 body = linear_color(0xe8704a);
    Vec3 head = linear_color(0xef7f55);
    Vec3 cream = linear_color(0xf6e7cc);
    Vec3 boots = linear_color(0x2b3a4c);
    Vec3 eyes = linear_color(0x1a1d26);
    Vec3 cheeks = linear_color(0xf59a8c);
    Vec3 gold = {1, 0.57F, 0.08F}; // Matches the sparks.
    Vec3 dust = linear_color(0xd9d0c1);
};

} // namespace

SceneMesh make_part_mesh(PartMesh kind) {
    SceneMesh mesh;
    // Soft plastic, or glossy for eyes and the antenna tip.
    mesh.surface = kind == PartMesh::glossy ? Surface{0.12F, 0} : Surface{0.5F, 0};
    constexpr int k_sides = 32;
    constexpr int k_rows = 16;
    const auto at = [](int row, int side) {
        const float latitude = k_pi * (static_cast<float>(row) / k_rows - 0.5F);
        const float longitude = k_tau * static_cast<float>(side) / k_sides;
        return Vec3{std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                    std::cos(latitude) * std::sin(longitude)};
    };
    for (int row = 0; row < k_rows; ++row) {
        for (int side = 0; side < k_sides; ++side) {
            const Vec3 a = at(row, side), b = at(row, side + 1);
            const Vec3 c = at(row + 1, side + 1), d = at(row + 1, side);
            // Counter-clockwise seen from outside; on a unit sphere the normal is the point.
            for (const Vec3 p : {a, d, c, a, c, b})
                mesh.vertices.push_back({p, p, {1, 1, 1}, mesh.surface});
        }
    }
    return mesh;
}

void CharacterAnimator::snap(const PlayerState &player) {
    m_current = {};
    m_current.air = player.grounded ? 0.0F : 1.0F;
    m_previous = m_current;
    m_started = true;
    m_position = player.position;
    m_motion = {};
    m_vertical_speed = player.velocity.y;
    m_facing = player.facing;
    m_grounded = player.grounded;
    m_state = player.state;
    m_diving = false;
    m_kick_normal = player.last_kick_normal;
    m_squash_rate = 0;
    m_antenna_rate = {};
    m_clock = 0;
    m_ticks = 0;
    m_dt = 0;
    m_puffs = {};
    m_next_puff = 0;
}

void CharacterAnimator::cheer() {
    m_current.glow = 1;
    m_squash_rate += 2.5F;
}

void CharacterAnimator::emit(const Puff &puff) {
    m_puffs[m_next_puff] = puff;
    m_next_puff = (m_next_puff + 1) % m_puffs.size();
}

void CharacterAnimator::burst(const Burst &ring) {
    const auto &[at, count, speed, gold] = ring;
    // A golden-ratio turn per tick varies the ring without any randomness.
    const float offset = k_tau * std::fmod(static_cast<float>(m_ticks) * 0.618034F, 1.0F);
    for (int i = 0; i < count; ++i) {
        const float angle = offset + k_tau * static_cast<float>(i) / static_cast<float>(count);
        emit({.origin = at,
              .velocity = {std::cos(angle) * speed, gold ? 1.8F : 0.4F, std::sin(angle) * speed},
              .life = gold ? 0.55F : 0.5F,
              .size = gold ? 0.07F : 0.13F,
              .gold = gold});
    }
}

void CharacterAnimator::tick(const PlayerState &player, float dt) {
    if (!m_started || dt <= 0 ||
        length_squared(player.position - m_position) > k_teleport * k_teleport) {
        snap(player);
        return;
    }
    m_previous = m_current;
    m_dt = dt;
    ++m_ticks;
    auto &s = m_current;
    const Vec3 motion = (player.position - m_position) * (1 / dt);
    const float speed = length(horizontal(motion));
    const bool grounded = player.grounded;
    const bool landed = grounded && !m_grounded;
    const bool took_off = m_grounded && !grounded && player.velocity.y > 2;
    const bool kicked = player.state == MoveState::wall_jump &&
                        (m_state != MoveState::wall_jump ||
                         length_squared(player.last_kick_normal - m_kick_normal) > 1.0e-4F);
    const Vec3 forward = yaw_forward(player.facing);
    const Vec3 right = yaw_right(player.facing);

    for (auto &puff : m_puffs) {
        if (puff.life <= 0)
            continue;
        puff.age += dt;
        // Keep the final tick interval available to an interpolated render. The puff
        // may already be dead at alpha=1 while still visible at alpha=0.
        if (puff.age - dt >= puff.life)
            puff.life = 0;
    }

    // Run cycle: the feet travel with the ground, so the stride never slides.
    const float previous_phase = s.run_phase;
    if (grounded) {
        s.run_phase += speed * dt / (1.2F + 0.1F * speed);
        s.run_phase -= std::floor(s.run_phase);
    }
    s.stride =
        smooth(s.stride, grounded ? std::min(speed / 7, 1.0F) : 0.0F, grounded ? 12.0F : 5.0F, dt);
    s.air = grounded ? 0 : smooth(s.air, 1, 9, dt);
    s.rise = smooth(s.rise, std::clamp(player.velocity.y / 10, -1.0F, 1.0F), 20, dt);

    if (player.state == MoveState::long_jump)
        m_diving = true;
    if (grounded || kicked)
        m_diving = false;
    s.dive = smooth(s.dive, m_diving ? 1.0F : 0.0F, m_diving ? 18.0F : 14.0F, dt);
    s.skid = smooth(s.skid, player.state == MoveState::skid ? 1.0F : 0.0F, 16, dt);

    // Lean grows with speed, more for momentum past a run, and with acceleration.
    float lean = 0.1F * std::min(speed / k_run_speed, 1.5F);
    if (grounded) {
        const float accel = m_grounded ? (speed - length(horizontal(m_motion))) / dt : 0;
        lean = 0.12F * std::min(speed / k_run_speed, 1.0F) +
               0.2F * std::clamp((speed - k_run_speed) / 13, 0.0F, 1.0F) +
               std::clamp(accel * 0.004F, -0.06F, 0.06F);
    }
    s.lean = smooth(s.lean, lean, 10, dt);
    // A hint of bank into ground turns (about 7 degrees at most): more reads as toppling.
    // Large single-tick turns (kicks, skid ends) are not turning.
    const float turn = angle_difference(m_facing, player.facing);
    const float yaw_rate = std::abs(turn) < 0.5F ? turn / dt : 0;
    const float bank = grounded ? std::clamp(yaw_rate * speed * 0.003F, -0.12F, 0.12F) : 0.0F;
    s.bank = smooth(s.bank, bank, 6, dt);

    // A kick turns the body from the wall to its new facing, plus one full twirl.
    if (kicked) {
        s.spin = 3 * k_pi;
        m_squash_rate += 2;
        burst(
            {player.position + Vec3{0, 0.8F, 0} - player.last_kick_normal * 0.35F, 5, 1.8F, false});
    }
    s.spin = approach(s.spin * std::exp(-8 * dt), 0, 0.4F * dt);

    // Squash and stretch on a bouncy spring: impact squashes, takeoff and fast air stretch.
    m_clock = std::fmod(m_clock + dt, k_clock_wrap);
    s.breath = std::sin(k_tau * m_clock / 3.2F) * (1 - s.stride) * (1 - s.air);
    float target = 1 + 0.012F * s.breath;
    if (!grounded)
        target += std::min(std::abs(player.velocity.y) * 0.007F, 0.08F);
    if (landed) {
        const float impact = std::max(-m_vertical_speed, 0.0F);
        m_squash_rate -= std::min(impact * 0.45F, 10.0F);
        if (impact > 8)
            burst({player.position + Vec3{0, 0.05F, 0}, 8, 2.0F + impact * 0.08F, false});
    }
    if (took_off) {
        m_squash_rate += 2.0F;
        if (player.state == MoveState::long_jump)
            burst({player.position + Vec3{0, 0.05F, 0} - forward * 0.2F, 6, 2.4F, false});
        if (player.last_hop == HopTiming::clean) {
            s.glow = 1; // Momentum kept: say so where the player is looking.
            burst({player.position + Vec3{0, 0.15F, 0}, 10, 2.2F, true});
        }
    }
    constexpr float k_omega = 19, k_damping = 0.3F;
    m_squash_rate +=
        (-k_omega * k_omega * (s.squash - target) - 2 * k_damping * k_omega * m_squash_rate) * dt;
    s.squash = std::clamp(s.squash + m_squash_rate * dt, 0.62F, 1.3F);

    // Ground dust: braced skids, and each footfall once momentum carries past a run.
    if (player.state == MoveState::skid && speed > 2 && m_ticks % 3 == 0) {
        const float side = (m_ticks / 3) % 2 == 0 ? -1.0F : 1.0F;
        emit({.origin = player.position + forward * 0.25F + right * (side * 0.13F),
              .velocity = forward * 1.2F + right * (side * 0.5F) + Vec3{0, 0.5F, 0},
              .life = 0.4F,
              .size = 0.1F});
    }
    if (grounded && speed > k_run_speed + 1.5F) {
        for (const float plant : {0.25F, 0.75F}) {
            const bool crossed = previous_phase < s.run_phase
                                     ? previous_phase < plant && s.run_phase >= plant
                                     : previous_phase < plant || s.run_phase >= plant;
            if (!crossed)
                continue;
            const float side = plant < 0.5F ? -1.0F : 1.0F;
            emit({.origin = player.position + right * (side * 0.13F) - forward * 0.1F,
                  .velocity = forward * -1.5F + Vec3{0, 0.4F, 0},
                  .life = 0.35F,
                  .size = 0.09F});
        }
    }

    // The antenna tip lags behind acceleration on a looser spring.
    const Vec3 accel = (motion - m_motion) * (1 / dt);
    Vec3 local{dot(accel, right), accel.y, -dot(accel, forward)};
    if (length(local) > 80)
        local = normalized(local) * 80;
    constexpr float k_tip_omega = 16, k_tip_damping = 0.22F;
    m_antenna_rate += (s.antenna * (-k_tip_omega * k_tip_omega) -
                       m_antenna_rate * (2 * k_tip_damping * k_tip_omega) - local) *
                      dt;
    s.antenna += m_antenna_rate * dt;
    if (length(s.antenna) > 0.14F)
        s.antenna = normalized(s.antenna) * 0.14F;

    // Blink every few seconds, and squeeze the eyes shut on a hard landing.
    const float since_blink = std::fmod(m_clock, 3.7F);
    s.blink = since_blink > 3.55F ? std::sin(k_pi * (since_blink - 3.55F) / 0.15F) : 0.0F;
    s.blink = std::max(s.blink, std::clamp((0.86F - s.squash) * 5, 0.0F, 1.0F));
    s.glow = approach(s.glow, 0, 1.6F * dt);

    m_position = player.position;
    m_motion = motion;
    m_vertical_speed = player.velocity.y;
    m_facing = player.facing;
    m_grounded = grounded;
    m_state = player.state;
    m_kick_normal = player.last_kick_normal;
}

void CharacterAnimator::build(const PlayerPose &pose, float alpha,
                              std::vector<CharacterPart> &parts) const {
    static const Palette colors;
    const Pose s = blend(m_previous, m_current, alpha);
    const float run = s.stride * (1 - s.air);
    const float theta = k_tau * s.run_phase;
    const float bob = run * 0.055F * std::abs(std::cos(theta));
    // Grounded poses pivot at the feet; airborne ones and the dive turn about the middle of
    // the body (a chained long jump dives again before the air pose has blended back in).
    const float pivot = 0.72F * std::max(s.air, s.dive);
    const float lean = s.lean + s.dive * 1.25F - s.skid * 0.38F;
    const float width = 1 / std::sqrt(s.squash);
    const Mat4 frame = yaw_translation(pose.facing + s.spin, pose.position) *
                       translation({0, pivot, 0}) * rotation_z(-s.bank) * rotation_x(-lean) *
                       scaling({width, s.squash, width}) * translation({0, -pivot, 0});
    const auto add = [&](const Mat4 &place, Vec3 radii, Vec3 tint,
                         PartMesh mesh = PartMesh::matte) {
        parts.push_back({.mesh = mesh, .model = frame * place * scaling(radii), .tint = tint});
    };

    // Body and belly, twisting a little with the arms.
    const float twist = 0.18F * run * std::sin(theta);
    const Mat4 body = translation({0, 0.56F + bob, 0}) * rotation_y(twist);
    add(body, {0.26F, 0.31F, 0.23F}, colors.body);
    add(body * translation({0, -0.04F, -0.16F}), {0.18F, 0.2F, 0.1F}, colors.cream);

    // Head, held a little more upright than the body.
    const Mat4 head = translation({0, 1.07F + bob * 0.7F, 0}) * rotation_x(0.3F * lean);
    add(head, {0.3F, 0.3F, 0.3F}, colors.head);
    for (const float side : {-1.0F, 1.0F}) {
        add(head * translation({side * 0.1F, 0.035F, -0.262F}),
            {0.045F, 0.078F * (1 - 0.9F * s.blink), 0.03F}, colors.eyes, PartMesh::glossy);
        add(head * translation({side * 0.175F, -0.055F, -0.225F}), {0.055F, 0.032F, 0.02F},
            colors.cheeks);
    }
    // Antenna: a stalk to a glowing tip that lags behind the body.
    const Vec3 base{0, 0.28F, 0};
    const Vec3 tip = Vec3{0, 0.5F, 0.05F} + s.antenna;
    const Vec3 axis = normalized(tip - base);
    const Vec3 across = normalized(cross(axis, {0, 0, 1}));
    add(head * translation((base + tip) * 0.5F) * basis(across, axis, cross(across, axis)),
        {0.02F, length(tip - base) * 0.5F, 0.02F}, colors.boots);
    parts.push_back({.mesh = PartMesh::glossy,
                     .model = frame * head * translation(tip) * scaling({0.06F, 0.06F, 0.06F}),
                     .tint = colors.gold,
                     .emissive = 0.6F + 2.4F * s.glow,
                     .casts_shadow = false});

    for (const float side : {-1.0F, 1.0F}) {
        // Each foot swings forward lifted and plants back; its hand swings the other way.
        const float phase = theta + (side > 0 ? k_pi : 0.0F);
        const float swing = std::sin(phase);
        const float lift = std::max(std::cos(phase), 0.0F);
        const float tuck = s.air * (1 - s.dive);
        const Vec3 foot{side * (0.13F - 0.04F * s.dive),
                        0.07F + 0.17F * run * lift + 0.16F * tuck + 0.02F * s.skid,
                        -0.3F * run * swing + tuck * (side < 0 ? -0.1F : 0.12F) + 0.12F * s.dive -
                            0.22F * s.skid};
        add(translation(foot) * rotation_x(0.5F * run * swing + 0.4F * s.skid),
            {0.105F, 0.075F, 0.155F}, colors.boots);

        const Vec3 hand{side * (0.37F + 0.1F * s.air - 0.15F * s.dive + 0.08F * s.skid),
                        0.56F + bob + 0.05F * run * std::abs(swing) +
                            s.air * (0.18F + 0.12F * std::abs(s.rise)) + 0.55F * s.dive +
                            0.18F * s.skid + 0.3F * s.glow,
                        0.22F * run * swing - 0.1F * s.dive + 0.12F * s.skid};
        add(translation(hand), {0.09F, 0.09F, 0.09F}, colors.cream);
    }
}

void CharacterAnimator::build_particles(float alpha,
                                        std::vector<ParticleInstance> &particles) const {
    static const Palette colors;
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    // Dust and golden motes live in world space, easing to rest as they fade. A
    // newborn belongs to the current fixed tick, never to the interval before it.
    for (const auto &puff : m_puffs) {
        if (puff.life <= 0)
            continue;
        const float t = puff.age - (1 - alpha) * m_dt;
        if (t <= 0 || t >= puff.life)
            continue;
        const float u = t / puff.life;
        const Vec3 at = puff.origin + puff.velocity * (t * (1 - 0.5F * u)) +
                        Vec3{0, puff.gold ? 0.0F : 0.35F * t, 0};
        const float fade = std::clamp(t / (puff.gold ? 0.018F : 0.04F), 0.0F, 1.0F);
        const float fade_in = fade * fade * (3 - 2 * fade);
        const float tail = 1 - u;
        particles.push_back({.position = at,
                             .radius = puff.size * (puff.gold ? 1 - 0.35F * u : 1 + 1.4F * u),
                             .color = puff.gold ? colors.gold : colors.dust,
                             .opacity = fade_in * (puff.gold ? 0.9F * tail : 0.5F * tail * tail),
                             .emissive = puff.gold ? 2.2F * tail : 0.0F});
    }
}

} // namespace mj::game
