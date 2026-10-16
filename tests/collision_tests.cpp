#include "test_framework.hpp"

#include <mj/collision/character_mover.hpp>
#include <mj/scene/mesh.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {
mj::CollisionWorld world_of(std::initializer_list<mj::Box> boxes,
                            std::initializer_list<mj::Ramp> ramps = {}) {
    mj::SceneMesh mesh;
    for (const auto &box : boxes)
        mesh.add_box(box);
    for (const auto &ramp : ramps)
        mesh.add_ramp(ramp);
    return mj::CollisionWorld(mesh.triangles());
}
const mj::Box k_floor{{0, -0.5F, 0}, {40, 1, 40}, {1, 1, 1}};

// Deterministic generator so the BVH comparison is reproducible.
struct Lcg {
    std::uint32_t state = 12345;
    float next(float low, float high) {
        state = state * 1664525U + 1013904223U;
        return low + (high - low) * static_cast<float>(state >> 8U) / 16777216.0F;
    }
};
} // namespace

TEST_CASE("closest point queries cover vertex, edge and face regions") {
    const mj::Triangle t{{0, 0, 0}, {2, 0, 0}, {0, 0, 2}};
    auto close = [](mj::Vec3 a, mj::Vec3 b) { return mj::length(a - b) < 1.0e-5F; };
    CHECK(close(mj::closest_point_on_triangle({-1, 1, -1}, t), {0, 0, 0}));
    CHECK(close(mj::closest_point_on_triangle({3, 5, -1}, t), {2, 0, 0}));
    CHECK(close(mj::closest_point_on_triangle({1, 2, -3}, t), {1, 0, 0}));
    CHECK(close(mj::closest_point_on_triangle({2, 0, 2}, t), {1, 0, 1}));
    CHECK(close(mj::closest_point_on_triangle({0.5F, 3, 0.5F}, t), {0.5F, 0, 0.5F}));

    const auto [p, q] =
        mj::closest_points_between({{{-1, 0, 0}, {1, 0, 0}}, {{0, 1, -1}, {0, 1, 1}}});
    CHECK(close(p, {0, 0, 0}));
    CHECK(close(q, {0, 1, 0}));
    const auto [r, s] =
        mj::closest_points_between({{{0, 0, 0}, {1, 0, 0}}, {{3, 2, 0}, {5, 2, 0}}});
    CHECK(close(r, {1, 0, 0}));
    CHECK(close(s, {3, 2, 0}));

    const auto crossing = mj::closest_points({{0.5F, -1, 0.5F}, {0.5F, 1, 0.5F}}, t);
    CHECK(crossing.distance_squared == doctest::Approx(0));
    const auto above = mj::closest_points({{0.5F, 1, 0.5F}, {0.5F, 3, 0.5F}}, t);
    CHECK(above.distance_squared == doctest::Approx(1));
    const auto beside = mj::closest_points({{-1, -1, 1}, {-1, 1, 1}}, t);
    CHECK(beside.distance_squared == doctest::Approx(1));
}

TEST_CASE("raycasts return the nearest surface with a normal facing the ray") {
    const auto world = world_of({k_floor, {{0, 2, -5}, {4, 4, 1}, {1, 1, 1}}});
    const auto down = world.raycast({{0, 5, 0}, {0, -1, 0}, 10});
    REQUIRE(down);
    CHECK(down->distance == doctest::Approx(5));
    CHECK(down->normal.y == doctest::Approx(1));
    const auto ahead = world.raycast({{0, 1, 0}, {0, 0, -1}, 10});
    REQUIRE(ahead);
    CHECK(ahead->distance == doctest::Approx(4.5F));
    CHECK(ahead->normal.z == doctest::Approx(1));
    CHECK_FALSE(world.raycast({{0, 1, 0}, {0, 0, -1}, 4}));
    CHECK_FALSE(world.raycast({{0, 1, 0}, {0, 1, 0}, 100}));
}

TEST_CASE("bounding volume hierarchy agrees with brute force") {
    Lcg random;
    std::vector<mj::Triangle> soup;
    for (int i = 0; i < 600; ++i) {
        const mj::Vec3 a{random.next(-30, 30), random.next(-5, 5), random.next(-30, 30)};
        soup.push_back({a, a + mj::Vec3{random.next(-2, 2), random.next(-2, 2), random.next(-2, 2)},
                        a + mj::Vec3{random.next(-2, 2), random.next(-2, 2), random.next(-2, 2)}});
    }
    const mj::CollisionWorld world(soup);
    int hits = 0;
    for (int i = 0; i < 300; ++i) {
        const mj::Vec3 origin{random.next(-35, 35), random.next(-8, 8), random.next(-35, 35)};
        const mj::Vec3 direction =
            mj::normalized({random.next(-1, 1), random.next(-1, 1), random.next(-1, 1)});
        const mj::Ray ray{origin, direction, 40};
        std::optional<float> brute;
        for (const auto &triangle : world.triangles()) {
            if (const auto d = mj::intersect(ray, triangle); d && (!brute || *d < *brute))
                brute = d;
        }
        const auto fast = world.raycast(ray);
        REQUIRE(fast.has_value() == brute.has_value());
        if (fast) {
            ++hits;
            CHECK(fast->distance == doctest::Approx(*brute));
        }
    }
    CHECK(hits > 20);
}

TEST_CASE("capsule sweeps stop at the skin, never tunnel and ignore separating motion") {
    const auto world = world_of({k_floor, {{0, 2, -5}, {6, 4, 0.05F}, {1, 1, 1}}});
    const mj::Capsule capsule{{0, 2, 0}, 0.35F, 1.6F};
    const auto drop = world.sweep({capsule, {0, -5, 0}, 0.01F});
    REQUIRE(drop);
    const float landed = 2 - 5 * drop->fraction;
    CHECK(landed >= 0.005F - 1.0e-5F);
    CHECK(landed <= 0.0101F);
    CHECK(drop->normal.y == doctest::Approx(1).epsilon(0.001));

    // 1 km in one step through a 5 cm wall.
    const mj::Capsule standing{{0, 0.5F, 0}, 0.35F, 1.6F};
    const auto fast = world.sweep({standing, {0, 0, -1000}, 0.01F});
    REQUIRE(fast);
    const float stop = -1000 * fast->fraction;
    CHECK(stop >= -(5 - 0.025F - 0.35F) + 0.0049F);
    CHECK(stop <= -(5 - 0.025F - 0.35F) + 0.0101F);
    CHECK(fast->normal.z == doctest::Approx(1).epsilon(0.001));

    mj::Capsule resting = capsule;
    resting.base.y = 0.008F;
    CHECK_FALSE(world.sweep({resting, {3, 0, 0}, 0.01F})); // Sliding along the floor.
    CHECK_FALSE(world.sweep({resting, {0, 2, 0}, 0.01F})); // Lifting off.
    CHECK(world.sweep({resting, {0, -1, 0}, 0.01F}));      // Pushing into it.

    mj::Capsule buried = capsule;
    buried.base.y = -0.2F;
    const auto push = world.depenetration(buried, 0.01F);
    CHECK(push.y == doctest::Approx(0.205F).epsilon(0.02));
}

TEST_CASE("character mover walks, slides, steps and respects the slope limit") {
    const mj::MoveSettings settings;
    const mj::Capsule body{{0, 0.005F, 0}, 0.35F, 1.6F};
    auto walk = [&](const mj::CollisionWorld &world, mj::Capsule capsule, mj::Vec3 step,
                    int ticks) {
        mj::MoveResult result{};
        result.base = capsule.base;
        result.grounded = true;
        for (int i = 0; i < ticks; ++i) {
            capsule.base = result.base;
            result = mj::move_character(world, settings, {capsule, step, result.grounded});
        }
        return result;
    };

    SUBCASE("flat ground: full distance, stays grounded at skin height") {
        const auto world = world_of({k_floor});
        const auto result = walk(world, body, {0.075F, 0, 0}, 100);
        CHECK(result.grounded);
        CHECK(result.base.x == doctest::Approx(7.5F));
        CHECK(result.base.y >= 0.0049F);
        CHECK(result.base.y <= 0.0101F);
    }
    SUBCASE("disabling ground snap still detects the floor directly beneath the feet") {
        const auto world = world_of({k_floor});
        auto no_snap = settings;
        no_snap.ground_snap = 0;
        auto capsule = body;
        for (int tick = 0; tick < 20; ++tick) {
            const auto result = mj::move_character(world, no_snap, {capsule, {0.075F, 0, 0}, true});
            REQUIRE(result.grounded);
            CHECK(result.base.y == doctest::Approx(body.base.y).epsilon(0.01));
            capsule.base = result.base;
        }
    }
    SUBCASE("head-on wall stops; diagonal approach slides along it") {
        const auto world = world_of({k_floor, {{0, 2, -3}, {20, 4, 1}, {1, 1, 1}}});
        const auto head_on = walk(world, body, {0, 0, -0.075F}, 100);
        CHECK(head_on.hit_wall);
        CHECK(head_on.base.z > -2.5F + 0.35F);
        CHECK(head_on.base.z < -2.5F + 0.35F + 0.011F);
        const auto diagonal = walk(world, body, {0.053F, 0, -0.053F}, 100);
        CHECK(diagonal.base.x == doctest::Approx(5.3F).epsilon(0.01));
        CHECK(diagonal.base.z > -2.5F + 0.35F);
    }
    SUBCASE("corners hold position without jitter") {
        const auto world = world_of(
            {k_floor, {{0, 2, -3}, {20, 4, 1}, {1, 1, 1}}, {{-3, 2, 0}, {1, 4, 20}, {1, 1, 1}}});
        const auto settled = walk(world, body, {-0.053F, 0, -0.053F}, 120);
        const auto later = walk(world, {settled.base, 0.35F, 1.6F}, {-0.053F, 0, -0.053F}, 60);
        CHECK(mj::length(later.base - settled.base) < 1.0e-4F);
        CHECK(later.base.x > -2.5F + 0.35F);
        CHECK(later.base.z > -2.5F + 0.35F);
    }
    SUBCASE("steps up to step_height are climbed, taller ones block") {
        const auto low = world_of({k_floor, {{0, 0.15F, -3}, {4, 0.3F, 2}, {1, 1, 1}}});
        const auto climbed = walk(low, body, {0, 0, -0.075F}, 40); // Ends on top.
        CHECK(climbed.grounded);
        CHECK(climbed.base.y == doctest::Approx(0.3F).epsilon(0.05));
        CHECK(climbed.base.z < -2.5F);
        const auto high = world_of({k_floor, {{0, 0.25F, -3}, {4, 0.5F, 2}, {1, 1, 1}}});
        const auto blocked = walk(high, body, {0, 0, -0.075F}, 60);
        CHECK(blocked.base.y < 0.05F);
        CHECK(blocked.base.z > -2 + 0.35F - 0.01F);
    }
    SUBCASE("walkable ramps are climbed and descended grounded; steep ones are not") {
        // Ramp from z = -2 rising toward -Z onto a 3 m deck that extends to z = -40.
        auto run_of = [](float degrees) { return 3 / std::tan(degrees * mj::k_pi / 180); };
        auto ramp = [&](float degrees) {
            return mj::Ramp{
                {0, 1.5F, -2 - run_of(degrees) * 0.5F}, {4, 3, run_of(degrees)}, {1, 1, 1}};
        };
        auto deck = [&](float degrees) {
            const float start = -2 - run_of(degrees);
            return mj::Box{{0, 1.5F, (start - 40) * 0.5F}, {4, 3, start + 40}, {1, 1, 1}};
        };
        for (const float degrees : {15.0F, 30.0F, 45.0F}) {
            CAPTURE(degrees);
            const auto world = world_of({k_floor, deck(degrees)}, {ramp(degrees)});
            const auto up = walk(world, body, {0, 0, -0.06F}, 400);
            CHECK(up.base.y > 2.9F);
            CHECK(up.grounded);
            // Walk back down: grounded every tick.
            mj::MoveResult down = up;
            for (int i = 0; i < 400; ++i) {
                down = mj::move_character(world, settings,
                                          {{down.base, 0.35F, 1.6F}, {0, 0, 0.06F}, true});
                REQUIRE(down.grounded);
            }
            CHECK(down.base.y < 0.02F);
        }
        const auto steep = world_of({k_floor, deck(58)}, {ramp(58)});
        const auto blocked = walk(steep, body, {0, 0, -0.06F}, 400);
        CHECK(blocked.base.y < settings.step_height + 0.05F);
    }
    SUBCASE("walking off a ledge leaves the ground") {
        const auto world = world_of({{{0, -0.5F, 0}, {4, 1, 4}, {1, 1, 1}}});
        const auto result = walk(world, body, {0.075F, 0, 0}, 60);
        CHECK_FALSE(result.grounded);
    }
}

TEST_CASE("a V between faces too steep to stand on holds a falling capsule; a steep one doesn't") {
    const mj::MoveSettings settings;
    // Two 60-degree slopes meeting along the X axis: each alone is too steep to stand on.
    const float rise = 2 * std::tan(mj::k_pi / 3);
    const auto valley = world_of({}, {{{0, rise * 0.5F, -1}, {6, rise, 2}, {1, 1, 1}, 0},
                                      {{0, rise * 0.5F, 1}, {6, rise, 2}, {1, 1, 1}, mj::k_pi}});
    mj::Capsule shape{{0, 1.5F, 0.05F}, 0.35F, 1.6F};
    bool held = false;
    for (int step = 0; step < 60 && !held; ++step) {
        const auto moved = mj::move_character(valley, settings, {shape, {0, -0.05F, 0}, false});
        shape.base = moved.base;
        held = moved.grounded;
    }
    REQUIRE(held);
    const float resting = shape.base.y;
    CHECK(resting > 0.0F); // Wedged between the slopes, above the crease line itself.
    // Standing there stays supported (so the player can jump out) and does not sink.
    const auto standing = mj::move_character(valley, settings, {shape, {0, 0, 0}, true});
    CHECK(standing.grounded);
    CHECK(standing.base.y == doctest::Approx(resting).epsilon(0.01));
    // Pushing along the valley still walks along it.
    const auto along = mj::move_character(valley, settings, {shape, {0.1F, 0, 0}, true});
    CHECK(along.base.x > shape.base.x + 0.05F);

    // A corner between two walls is a vertical crease: falling into it keeps falling.
    const auto corner = world_of(
        {k_floor, {{-1, 5, 0}, {0.5F, 10, 6}, {1, 1, 1}}, {{0, 5, -1}, {6, 10, 0.5F}, {1, 1, 1}}});
    mj::Capsule wedged{{-0.39F, 4, -0.39F}, 0.35F, 1.6F};
    const auto fell =
        mj::move_character(corner, settings, {wedged, {-0.05F, -0.2F, -0.05F}, false});
    CHECK_FALSE(fell.grounded);
    CHECK(fell.base.y < 3.85F);
}
