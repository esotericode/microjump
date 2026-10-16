#include "test_framework.hpp"

#include <character.hpp>
#include <level_io.hpp>
#include <simulation.hpp>

#include <mj/core/input_record.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using mj::ParticleInstance;
using mj::Vec3;
using mj::game::CharacterAnimator;
using mj::game::CharacterPart;

namespace {
constexpr float k_dt = 1.0F / 120;

float determinant(const mj::Mat4 &m) {
    const auto at = [&](std::size_t row, std::size_t column) { return m.at(row, column); };
    return at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
           at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
           at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
}
Vec3 origin(const mj::Mat4 &m) {
    return {m.at(0, 3), m.at(1, 3), m.at(2, 3)};
}
mj::CollisionWorld floor_world() {
    mj::SceneMesh mesh;
    mesh.add_box({{0, -0.5F, 0}, {200, 1, 200}, {1, 1, 1}});
    return mj::CollisionWorld(mesh.triangles());
}

// The highlights of a replay, as the animator saw them.
struct Seen {
    float deepest_squash = 1;
    float highest_stretch = 1;
    float deepest_dive = 0;
    float largest_spin = 0;
    float brightest_glow = 0;
    float largest_stride = 0;
    std::size_t most_particles = 0;
};

Seen animate_replay(const std::string &file) {
    const auto level =
        mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / "playground.json");
    REQUIRE(level);
    const auto replay = mj::load_replay(std::filesystem::path(MJ_TEST_REPLAYS) / file);
    REQUIRE(replay);
    const auto geometry = mj::game::build_geometry(*level);
    mj::game::Simulation simulation(*level, geometry.collision);
    CharacterAnimator character;
    character.snap(simulation.player.current());
    Seen seen;
    std::vector<CharacterPart> parts;
    std::vector<ParticleInstance> particles;
    for (const auto &input : *replay) {
        simulation.tick(input, k_dt);
        character.tick(simulation.player.current(), k_dt);
        const auto &pose = character.current();
        seen.deepest_squash = std::min(seen.deepest_squash, pose.squash);
        seen.highest_stretch = std::max(seen.highest_stretch, pose.squash);
        seen.deepest_dive = std::max(seen.deepest_dive, pose.dive);
        seen.largest_spin = std::max(seen.largest_spin, pose.spin);
        seen.brightest_glow = std::max(seen.brightest_glow, pose.glow);
        seen.largest_stride = std::max(seen.largest_stride, pose.stride);
        for (const float alpha : {0.0F, 0.5F, 1.0F}) {
            const auto feet = simulation.player.interpolated(alpha);
            parts.clear();
            character.build(feet, alpha, parts);
            REQUIRE(parts.size() == CharacterAnimator::k_body_parts);
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const auto &model = parts[i].model;
                for (const float value : model.values)
                    REQUIRE(std::isfinite(value));
                // Never mirrored or flattened, so the shader's cofactor normals stay outward.
                REQUIRE(determinant(model) > 0.0F);
                // Every body part stays with the collision capsule it stands in for.
                const Vec3 offset = origin(model) - feet.position;
                CAPTURE(simulation.ticks());
                CAPTURE(i);
                CHECK(std::hypot(offset.x, offset.z) < 1.1F);
                CHECK(offset.y > -0.45F);
                CHECK(offset.y < 2.0F);
            }
            particles.clear();
            character.build_particles(alpha, particles);
            REQUIRE(particles.size() <= CharacterAnimator::k_max_puffs);
            seen.most_particles = std::max(seen.most_particles, particles.size());
            for (const auto &particle : particles) {
                for (const float value :
                     {particle.position.x, particle.position.y, particle.position.z,
                      particle.radius, particle.color.x, particle.color.y, particle.color.z,
                      particle.opacity, particle.emissive})
                    REQUIRE(std::isfinite(value));
                CHECK(particle.radius > 0.0F);
                CHECK(particle.radius < 0.4F);
                CHECK(particle.opacity > 0.0F);
                CHECK(particle.opacity <= 1.0F);
                CHECK(particle.emissive >= 0.0F);
            }
        }
    }
    return seen;
}

mj::game::PlayerState begin_clean_hop(CharacterAnimator &character) {
    character.snap({});
    mj::game::PlayerState airborne;
    airborne.velocity = {0, 10, 0};
    airborne.state = mj::game::MoveState::jump;
    airborne.grounded = false;
    airborne.last_hop = mj::game::HopTiming::clean;
    character.tick(airborne, k_dt);
    return airborne;
}

void check_same_particles(const std::vector<ParticleInstance> &a,
                          const std::vector<ParticleInstance> &b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(mj::length(a[i].position - b[i].position) < 1.0e-6F);
        CHECK(std::abs(a[i].radius - b[i].radius) < 1.0e-6F);
        CHECK(mj::length(a[i].color - b[i].color) < 1.0e-6F);
        CHECK(std::abs(a[i].opacity - b[i].opacity) < 1.0e-6F);
        CHECK(std::abs(a[i].emissive - b[i].emissive) < 1.0e-6F);
    }
}
} // namespace

TEST_CASE("the character performs the committed move tours without leaving its capsule") {
    const auto tour = animate_replay("m4-moves-tour.mjr");
    CHECK(tour.largest_stride > 0.99F);  // A full run cycle.
    CHECK(tour.deepest_dive > 0.9F);     // The long jump's dive.
    CHECK(tour.largest_spin > 8.5F);     // Wall kicks twirl (3 pi, less a tick of easing).
    CHECK(tour.deepest_squash < 0.92F);  // Landings squash...
    CHECK(tour.highest_stretch > 1.05F); // ...and takeoffs stretch.
    CHECK(tour.most_particles > 0);      // Dust was kicked up.
    // Clean hops at speed flash the antenna, so kept momentum is visible.
    const auto chain = animate_replay("m4-momentum-chain.mjr");
    CHECK(chain.brightest_glow > 0.9F);
}

TEST_CASE("landings squash with the fall height, then the spring settles") {
    const auto world = floor_world();
    const auto drop = [&](float height) {
        mj::game::Player player;
        player.place({0, height, 0}, 0);
        CharacterAnimator character;
        character.snap(player.current());
        float deepest = 1;
        for (int i = 0; i < 240; ++i) {
            player.tick({}, world, k_dt);
            character.tick(player.current(), k_dt);
            deepest = std::min(deepest, character.current().squash);
        }
        // Two seconds in, only the gentle idle breathing remains.
        CHECK(std::abs(character.current().squash - 1) < 0.02F);
        CHECK(character.current().air == 0.0F);
        return deepest;
    };
    const float step = drop(0.3F);
    const float jump = drop(2.5F);
    const float tower = drop(12.0F);
    CHECK(step > 0.9F);
    CHECK(jump < step);
    CHECK(tower < jump);
    CHECK(tower >= 0.62F); // Clamped: the character never folds flat.
}

TEST_CASE("respawns snap the character instead of smearing it across the level") {
    const auto world = floor_world();
    mj::game::Player player;
    player.place({0, 0, 0}, 0);
    CharacterAnimator character;
    character.snap(player.current());
    for (int i = 0; i < 120; ++i) {
        player.tick({0, 1, 0, 0, 0}, world, k_dt); // Run, then respawn far away.
        character.tick(player.current(), k_dt);
    }
    REQUIRE(character.current().stride > 0.5F);
    player.place({40, 5, 40}, 1);
    player.tick({}, world, k_dt);
    character.tick(player.current(), k_dt);
    CHECK(character.current().stride == 0.0F);
    std::vector<CharacterPart> before, after;
    const auto pose = player.interpolated(1);
    character.build(pose, 0, before);
    character.build(pose, 1, after);
    REQUIRE(before.size() == after.size());
    for (std::size_t i = 0; i < before.size(); ++i)
        CHECK(before[i].model.values == after[i].model.values);
    std::vector<ParticleInstance> particles;
    character.build_particles(0.5F, particles);
    CHECK(particles.empty());
}

TEST_CASE("character particles fade across fixed-tick births and deaths") {
    CharacterAnimator character;
    const auto airborne = begin_clean_hop(character);
    std::vector<ParticleInstance> particles;
    // This takeoff happened at the current endpoint. Its motes must not appear
    // throughout the previous tick, and their fade starts from zero at birth.
    for (const float alpha : {0.0F, 0.5F, 1.0F}) {
        character.build_particles(alpha, particles);
        CHECK(particles.empty());
    }
    character.tick(airborne, k_dt);
    character.build_particles(0, particles);
    CHECK(particles.empty());
    character.build_particles(0.5F, particles);
    REQUIRE(particles.size() == 10);
    const auto halfway = particles;
    particles.clear();
    character.build_particles(1, particles);
    REQUIRE(particles.size() == halfway.size());
    CHECK(particles[0].opacity > halfway[0].opacity);
    CHECK(particles[0].position.y > halfway[0].position.y);

    bool saw_fading_tail = false;
    bool saw_final_interval = false;
    float peak_opacity = particles[0].opacity;
    for (int tick = 0; tick < 80; ++tick) {
        CAPTURE(tick);
        const auto previous_endpoint = particles;
        character.tick(airborne, k_dt);
        particles.clear();
        character.build_particles(0, particles);
        // In particular, the final interpolation interval must not be dropped
        // merely because the newest fixed-tick state has passed the lifetime.
        check_same_particles(particles, previous_endpoint);
        particles.clear();
        character.build_particles(1, particles);
        if (particles.empty()) {
            saw_final_interval = saw_final_interval || !previous_endpoint.empty();
            continue;
        }
        peak_opacity = std::max(peak_opacity, particles[0].opacity);
        saw_fading_tail = saw_fading_tail || particles[0].opacity < peak_opacity * 0.1F;
    }
    CHECK(saw_fading_tail);
    CHECK(saw_final_interval);
    CHECK(particles.empty());
}

TEST_CASE("particle presentation is cadence independent and snaps reset the emitter") {
    CharacterAnimator sparse, frequent;
    const auto airborne = begin_clean_hop(sparse);
    begin_clean_hop(frequent);
    std::vector<ParticleInstance> sparse_particles, frequent_particles;
    for (int tick = 0; tick < 24; ++tick) {
        sparse.tick(airborne, k_dt);
        frequent.tick(airborne, k_dt);
        for (const float alpha : {0.0F, 0.2F, 0.45F, 0.8F, 1.0F}) {
            frequent_particles.clear();
            frequent.build_particles(alpha, frequent_particles);
        }
        sparse_particles.clear();
        sparse.build_particles(1, sparse_particles);
        check_same_particles(sparse_particles, frequent_particles);
    }
    // A reset forgets both live puffs and the burst phase. Matching input after
    // different histories starts exactly the same visible celebration.
    begin_clean_hop(sparse);
    CharacterAnimator fresh;
    begin_clean_hop(fresh);
    sparse.tick(airborne, k_dt);
    fresh.tick(airborne, k_dt);
    sparse_particles.clear();
    frequent_particles.clear();
    sparse.build_particles(0.75F, sparse_particles);
    fresh.build_particles(0.75F, frequent_particles);
    REQUIRE(sparse_particles.size() == 10);
    check_same_particles(sparse_particles, frequent_particles);
}
