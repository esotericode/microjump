#include "test_framework.hpp"

#include <mj/render/particles.hpp>

#include <array>
#include <limits>

namespace {
mj::ParticleInstance particle_at(mj::Vec3 position, float marker = 1) {
    return {.position = position, .radius = 0.2F, .color = {marker, 0, 0}, .opacity = 0.5F};
}
} // namespace

TEST_CASE("particles sort by camera depth, keep equal-depth order and leave input alone") {
    // The distant off-axis particle is closer in camera depth. Euclidean distance
    // would blend it in the wrong order, especially at wide camera angles.
    const std::array submitted{particle_at({100, 0, -2}, 1), particle_at({0, 0, -8}, 2),
                               particle_at({1, 0, -8}, 3), particle_at({0, 0, -4}, 4)};
    const auto sorted = mj::prepare_particles(submitted, {}, {0, 0, -1});
    REQUIRE(sorted.size() == submitted.size());
    CHECK(sorted[0].color.x == 2.0F);
    CHECK(sorted[1].color.x == 3.0F); // Stable ties preserve the caller's intended layering.
    CHECK(sorted[2].color.x == 4.0F);
    CHECK(sorted[3].color.x == 1.0F);
    CHECK(submitted[0].color.x == 1.0F);
    CHECK(submitted[1].color.x == 2.0F);

    // A translated camera looking along +X uses its own forward axis, not world Z.
    const std::array turned{particle_at({6, 2, 20}, 1), particle_at({12, 2, 3}, 2),
                            particle_at({9, 2, -10}, 3)};
    const auto from_side = mj::prepare_particles(turned, {5, 2, 3}, {1, 0, 0});
    REQUIRE(from_side.size() == turned.size());
    CHECK(from_side[0].color.x == 2.0F);
    CHECK(from_side[1].color.x == 3.0F);
    CHECK(from_side[2].color.x == 1.0F);
}

TEST_CASE("invalid and invisible particle data never reaches a draw") {
    constexpr float infinity = std::numeric_limits<float>::infinity();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    auto valid = particle_at({0, 0, -3});
    std::array submitted{valid, valid, valid, valid, valid, valid,
                         valid, valid, valid, valid, valid, valid};
    submitted[1].radius = 0;
    submitted[2].radius = -1;
    submitted[3].radius = infinity;
    submitted[4].position.x = nan;
    submitted[5].color.y = infinity;
    submitted[6].color.z = -0.1F;
    submitted[7].opacity = 0;
    submitted[8].opacity = nan;
    submitted[9].emissive = infinity;
    submitted[10].emissive = -1;
    submitted[11].opacity = 2;
    const auto sorted = mj::prepare_particles(submitted, {}, {0, 0, -1});
    REQUIRE(sorted.size() == 2);
    CHECK(sorted[0].opacity == 0.5F);
    CHECK(sorted[1].opacity == 1.0F);
    CHECK(submitted[11].opacity == 2.0F); // Clamping affects only the draw copy.

    CHECK(mj::prepare_particles(submitted, {nan, 0, 0}, {0, 0, -1}).empty());
    CHECK(mj::prepare_particles(submitted, {}, {0, infinity, -1}).empty());
    valid.position = {std::numeric_limits<float>::max(), 0, 0};
    const std::array overflow{valid};
    CHECK(mj::prepare_particles(overflow, {-std::numeric_limits<float>::max(), 0, 0}, {1, 0, 0})
              .empty()); // Finite inputs can still overflow the sorting depth.
}

TEST_CASE("particle submission has a fixed renderer budget") {
    std::vector<mj::ParticleInstance> submitted(mj::k_max_particles + 12);
    for (std::size_t i = 0; i < submitted.size(); ++i)
        submitted[i] = particle_at({0, 0, -static_cast<float>(i + 1)});
    const auto sorted = mj::prepare_particles(submitted, {}, {0, 0, -1});
    REQUIRE(sorted.size() == mj::k_max_particles);
    CHECK(sorted.front().position.z == -static_cast<float>(mj::k_max_particles));
    CHECK(sorted.back().position.z == -1.0F);
    CHECK(mj::prepare_particles({}, {}, {0, 0, -1}).empty());
}
