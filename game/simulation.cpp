#include "simulation.hpp"

namespace mj::game {

Simulation::Simulation(const Level &level, const CollisionWorld &world)
    : m_level(level), m_world(world) {
    const auto spawn = level.checkpoints.empty() ? Checkpoint{} : level.checkpoints.front();
    player.place(spawn.position, spawn.yaw);
    camera.snap(player.current(), spawn.yaw);
}

void Simulation::respawn() {
    const auto spawn =
        m_level.checkpoints.empty() ? Checkpoint{} : m_level.checkpoints[m_checkpoint];
    player.place(spawn.position, spawn.yaw);
    camera.snap(player.current(), spawn.yaw);
    ++m_respawns;
}

void Simulation::tick(const TickInput &input, float dt) {
    ++m_ticks;
    camera.set_orientation({input.camera_yaw, input.camera_pitch});
    if (input.held(input_button::reset_pressed)) {
        respawn();
        return;
    }
    player.tick(input, m_world, dt);
    const auto &state = player.current();
    if (state.position.y < m_level.kill_height) {
        respawn();
        return;
    }
    if (state.grounded) {
        for (std::size_t i = 0; i < m_level.checkpoints.size(); ++i) {
            const Vec3 offset = state.position - m_level.checkpoints[i].position;
            if (length(horizontal(offset)) < 1.0F && std::abs(offset.y) < 0.5F)
                m_checkpoint = i;
        }
    }
    camera.tick(state, m_world, dt);
}

} // namespace mj::game
