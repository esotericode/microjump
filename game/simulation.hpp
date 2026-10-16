#pragma once

#include "follow_camera.hpp"
#include "level.hpp"
#include "player.hpp"

#include <cstdint>
#include <optional>

namespace mj::game {

// Everything advanced by the fixed 120 Hz tick. Given the same level, tuning and TickInput
// sequence it produces bit-identical results; render rate never enters it.
class Simulation {
  public:
    // Starts at `checkpoint` (clamped to the level's), which also becomes the respawn point.
    Simulation(const Level &level, const CollisionWorld &world, std::size_t checkpoint = 0);

    void tick(const TickInput &input, float dt);
    void respawn();

    // The world with its movers where this tick left them.
    [[nodiscard]] CollisionScene scene() const {
        return {m_world, m_poses, m_previous_poses};
    }
    // Mover i between the last two ticks, for drawing.
    [[nodiscard]] BodyPose mover_pose(std::size_t index, float alpha) const;
    [[nodiscard]] double seconds() const {
        return m_seconds;
    }

    Player player;
    FollowCamera camera;
    [[nodiscard]] std::size_t active_checkpoint() const {
        return m_checkpoint;
    }
    [[nodiscard]] std::uint64_t ticks() const {
        return m_ticks;
    }
    [[nodiscard]] std::uint32_t respawns() const {
        return m_respawns;
    }
    [[nodiscard]] bool collectible_collected(std::size_t index) const {
        return index < m_collected.size() && m_collected[index];
    }
    [[nodiscard]] std::size_t collected_count() const {
        return m_collected_count;
    }
    [[nodiscard]] std::size_t collectible_count() const {
        return m_collected.size();
    }
    [[nodiscard]] std::optional<std::size_t> last_collected() const {
        return m_last_collected;
    }
    [[nodiscard]] std::uint64_t last_collection_tick() const {
        return m_last_collection_tick;
    }
    // Which sparks are found, to carry a hunt across a trip to another level and back.
    [[nodiscard]] const std::vector<bool> &collected() const {
        return m_collected;
    }
    void restore_collected(const std::vector<bool> &collected);
    // The portal the player stepped into on the last tick, if any. Only the step in counts,
    // so arriving inside a gate never sends you straight back. Gameplay ignores it: travel
    // is the game's decision (live play only), so replays keep their bits.
    [[nodiscard]] std::optional<std::size_t> portal_entered() const {
        return m_portal_entered;
    }

  private:
    void collect_sparks(const Capsule &before);
    void enter_portals(const Capsule &before);

    const Level &m_level;
    const CollisionWorld &m_world;
    double m_seconds = 0; // Simulation time, which alone decides where movers are.
    std::vector<BodyPose> m_poses;
    std::vector<BodyPose> m_previous_poses;
    std::size_t m_checkpoint = 0;
    std::uint64_t m_ticks = 0;
    std::uint32_t m_respawns = 0;
    std::vector<bool> m_collected; // Survives respawn; a new Simulation starts a new hunt.
    std::size_t m_collected_count = 0;
    std::optional<std::size_t> m_last_collected;
    std::uint64_t m_last_collection_tick = 0;
    std::optional<std::size_t> m_portal_entered;
};

} // namespace mj::game
