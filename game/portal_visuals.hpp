#pragma once

#include "level.hpp"

#include <mj/render/particles.hpp>
#include <mj/scene/mesh.hpp>

#include <vector>

namespace mj::game {

// A unit-radius ring in the XY plane (its opening faces ±Z), drawn tinted and emissive as a
// gate's rim. It has no collision geometry: the gate is a trigger, not a wall.
[[nodiscard]] SceneMesh make_portal_ring_mesh();

// Where a gate's rim and veil are drawn this frame, and how bright they glow. The veil is
// the shared unit sphere (make_part_mesh) flattened into a standing disc.
struct PortalLook {
    Mat4 ring;
    Mat4 veil;
    float glow = 1;
};
[[nodiscard]] PortalLook portal_look(const Portal &portal, float seconds);
// Soft motes drifting in from the rim toward the middle of the gate.
void portal_motes(const Portal &portal, float seconds, std::vector<ParticleInstance> &out);

} // namespace mj::game
