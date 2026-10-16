#pragma once

#include "player.hpp"

#include <mj/render/particles.hpp>
#include <mj/scene/mesh.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace mj::game {

// The two shared unit spheres every solid character part is drawn with.
enum class PartMesh : std::uint8_t { matte, glossy };
[[nodiscard]] SceneMesh make_part_mesh(PartMesh kind);

struct CharacterPart {
    PartMesh mesh = PartMesh::matte;
    Mat4 model; // Unit sphere to world: rotation, per-axis scale and translation.
    Vec3 tint{1, 1, 1};
    float emissive = 0;
    bool casts_shadow = true;
};

// The little creature drawn in place of the collision capsule. It is purely visual: it
// reads the player's state once per fixed tick and never feeds back into gameplay, so every
// replay keeps its bits. Ticking on the fixed step and interpolating like the player keeps
// tick captures reproducible at any frame rate. See docs/character.md.
class CharacterAnimator {
  public:
    // Everything a frame blends between the last two ticks.
    struct Pose {
        float run_phase = 0; // Run cycle, in cycles wrapped to [0, 1).
        float stride = 0;    // Run swing, 0 standing .. 1 full run.
        float squash = 1;    // Height scale on a spring; width keeps the volume.
        float lean = 0;      // Forward pitch from speed and acceleration, radians.
        float bank = 0;      // Roll into turns, positive to the right.
        float air = 0;       // 0 grounded .. 1 airborne pose.
        float dive = 0;      // Long-jump pose.
        float skid = 0;      // Skid pose.
        float spin = 0;      // Remaining wall-kick twirl, radians.
        float rise = 0;      // Vertical speed mapped to -1 (falling) .. 1 (rising).
        float blink = 0;     // 0 open .. 1 shut.
        float breath = 0;    // Idle breathing, -1 .. 1.
        float glow = 0;      // Antenna flash after a clean hop or a found spark.
        float flip = 0;      // Forward somersault (triple jump, rollout), radians.
        float tuck = 0;      // Knees up for a somersault, 0 .. 1.
        float slide = 0;     // Lying on the belly after a dive, 0 .. 1.
        Vec3 antenna;        // Lag of the antenna tip, character space.
    };

    // Starts over at the player's current state (first frame, respawn, teleport).
    void snap(const PlayerState &player);
    void tick(const PlayerState &player, float dt);
    // A found spark: the antenna flashes and the body gives a happy bounce.
    void cheer();
    // Appends only the solid body for the interpolated player pose.
    void build(const PlayerPose &pose, float alpha, std::vector<CharacterPart> &parts) const;
    // Appends world-space dust and clean-hop motes for the transparent particle pass.
    void build_particles(float alpha, std::vector<ParticleInstance> &particles) const;

    [[nodiscard]] const Pose &current() const {
        return m_current;
    }

    // The solid body and transparent particle output are separately bounded.
    static constexpr std::size_t k_body_parts = 13;
    static constexpr std::size_t k_max_puffs = 24;

  private:
    struct Puff {
        Vec3 origin;
        Vec3 velocity; // Eases to rest over the puff's life.
        float age = 0;
        float life = 0; // Zero: unused.
        float size = 0;
        bool gold = false; // A clean-hop mote rather than dust.
    };
    void emit(const Puff &puff);
    struct Burst {
        Vec3 at;
        int count = 0;
        float speed = 0; // Outward, meters per second.
        bool gold = false;
    };
    void burst(const Burst &ring);

    Pose m_previous;
    Pose m_current;
    bool m_started = false;
    Vec3 m_position;
    Vec3 m_motion; // Actual displacement per second over the last tick.
    float m_vertical_speed = 0;
    float m_facing = 0;
    bool m_grounded = true;
    MoveState m_state = MoveState::idle;
    bool m_diving = false; // From a long-jump takeoff until the landing or a wall kick.
    Vec3 m_kick_normal;
    float m_flip_target = 0; // The somersault in progress ends here...
    float m_flip_rate = 0;   // ...turning at this many radians per second.
    float m_squash_rate = 0;
    Vec3 m_antenna_rate;
    float m_clock = 0; // Seconds since the last snap, wrapped; drives blinks and breathing.
    std::uint32_t m_ticks = 0;
    float m_dt = 0;
    std::array<Puff, k_max_puffs> m_puffs{};
    std::size_t m_next_puff = 0;
};

} // namespace mj::game
