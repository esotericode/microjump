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
    Simulation(const Level &level, const CollisionWorld &world);

    void tick(const TickInput &input, float dt);
    void respawn();

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

  private:
    void collect_sparks(const Capsule &before);

    const Level &m_level;
    const CollisionWorld &m_world;
    std::size_t m_checkpoint = 0;
    std::uint64_t m_ticks = 0;
    std::uint32_t m_respawns = 0;
    std::vector<bool> m_collected; // Survives respawn; a new Simulation starts a new hunt.
    std::size_t m_collected_count = 0;
    std::optional<std::size_t> m_last_collected;
    std::uint64_t m_last_collection_tick = 0;
};

} // namespace mj::game
