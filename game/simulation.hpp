#pragma once

#include "follow_camera.hpp"
#include "level.hpp"
#include "player.hpp"

#include <cstdint>

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

  private:
    const Level &m_level;
    const CollisionWorld &m_world;
    std::size_t m_checkpoint = 0;
    std::uint64_t m_ticks = 0;
    std::uint32_t m_respawns = 0;
};

} // namespace mj::game
