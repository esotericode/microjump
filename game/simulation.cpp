#include "simulation.hpp"

#include <algorithm>

namespace mj::game {
namespace {
bool visible(const CollisionScene &world, Vec3 from, Vec3 to) {
    const Vec3 delta = to - from;
    const float distance = length(delta);
    return distance <= 1.0e-6F || !world.raycast({from, delta * (1.0F / distance), distance});
}

bool touches(const CollisionScene &world, const Capsule &capsule, const Collectible &spark) {
    const Vec3 closest = closest_point_on_segment(spark.position, capsule.segment());
    const float reach = capsule.radius + spark.radius;
    return length_squared(spark.position - closest) <= reach * reach &&
           visible(world, closest, spark.position);
}

bool swept_touch(const CollisionScene &world, const Capsule &before, const Capsule &after,
                 const Collectible &spark) {
    if (touches(world, before, spark) || touches(world, after, spark))
        return true;
    const auto body = before.segment();
    const Vec3 travel = after.base - before.base;
    // Relative motion turns the swept vertical segment into a segment/segment query:
    // one segment follows the bottom cap; the other is the spark minus the body height.
    const auto [on_path, on_spark] =
        closest_points_between({{body.start, body.start + travel},
                                {spark.position - (body.end - body.start), spark.position}});
    const float reach = before.radius + spark.radius;
    if (length_squared(on_path - on_spark) > reach * reach)
        return false;
    const Vec3 on_body = on_path + (spark.position - on_spark);
    if (!visible(world, on_body, spark.position))
        return false;
    // Sliding and stepping can bend the actual path. Do not award an inferred point
    // on the straight chord through solid geometry; endpoint overlaps above stay valid.
    return !world.sweep({before, on_path - body.start, 0});
}

bool inside(const Capsule &capsule, const Portal &portal) {
    const Vec3 closest = closest_point_on_segment(portal.position, capsule.segment());
    const float reach = capsule.radius + portal.radius;
    return length_squared(portal.position - closest) <= reach * reach;
}
} // namespace

Simulation::Simulation(const Level &level, const CollisionWorld &world, std::size_t checkpoint)
    : m_level(level), m_world(world),
      m_checkpoint(level.checkpoints.empty() ? 0
                                             : std::min(checkpoint, level.checkpoints.size() - 1)),
      m_collected(level.collectibles.size(), false) {
    for (const auto &mover : level.movers)
        m_poses.push_back(game::mover_pose(mover, 0));
    m_previous_poses = m_poses;
    const auto spawn = level.checkpoints.empty() ? Checkpoint{} : level.checkpoints[m_checkpoint];
    player.place(spawn.position, spawn.yaw);
    camera.snap(player.current(), spawn.yaw);
}

void Simulation::restore_collected(const std::vector<bool> &collected) {
    if (collected.size() != m_collected.size())
        return; // A different version of the level: start its hunt fresh.
    m_collected = collected;
    m_collected_count =
        static_cast<std::size_t>(std::count(m_collected.begin(), m_collected.end(), true));
}

void Simulation::respawn() {
    const auto spawn =
        m_level.checkpoints.empty() ? Checkpoint{} : m_level.checkpoints[m_checkpoint];
    player.place(spawn.position, spawn.yaw);
    camera.snap(player.current(), spawn.yaw);
    ++m_respawns;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): an index and a 0..1 blend.
BodyPose Simulation::mover_pose(std::size_t index, float alpha) const {
    const auto &from = m_previous_poses[index];
    const auto &to = m_poses[index];
    return {lerp(from.position, to.position, alpha), lerp_angle(from.yaw, to.yaw, alpha)};
}

void Simulation::tick(const TickInput &input, float dt) {
    ++m_ticks;
    // Movers step first, so a rider is carried through this tick's motion.
    m_seconds += dt;
    m_previous_poses = m_poses;
    for (std::size_t i = 0; i < m_poses.size(); ++i)
        m_poses[i] = game::mover_pose(m_level.movers[i], m_seconds);
    m_portal_entered.reset();
    camera.set_orientation({input.camera_yaw, input.camera_pitch});
    if (input.held(input_button::reset_pressed)) {
        respawn();
        return;
    }
    const auto before = player.capsule();
    player.tick(input, scene(), dt);
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
    collect_sparks(before);
    enter_portals(before);
    camera.tick(state, scene(), dt);
}

void Simulation::enter_portals(const Capsule &before) {
    const auto after = player.capsule();
    for (std::size_t i = 0; i < m_level.portals.size(); ++i) {
        if (inside(after, m_level.portals[i]) && !inside(before, m_level.portals[i])) {
            m_portal_entered = i;
            return;
        }
    }
}

void Simulation::collect_sparks(const Capsule &before) {
    const auto after = player.capsule();
    for (std::size_t i = 0; i < m_collected.size(); ++i) {
        if (m_collected[i] || !swept_touch(scene(), before, after, m_level.collectibles[i]))
            continue;
        m_collected[i] = true;
        ++m_collected_count;
        m_last_collected = i;
        m_last_collection_tick = m_ticks;
    }
}

} // namespace mj::game
