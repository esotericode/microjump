#pragma once

#include <mj/scene/mesh.hpp>

namespace mj::game {

// A faceted golden spark, centered on its placement. It has no collision geometry.
[[nodiscard]] SceneMesh make_spark_mesh();

} // namespace mj::game
