#include "test_framework.hpp"

#include <player.hpp>

#include <mj/scene/mesh.hpp>

#include <vector>

namespace {
namespace button = mj::input_button;
constexpr float k_dt = 1.0F / 120;
using mj::game::MoveState;

mj::CollisionWorld floor_world() {
    mj::SceneMesh mesh;
    mesh.add_box({{0, -0.5F, 0}, {400, 1, 400}, {1, 1, 1}});
    return mj::CollisionWorld(mesh.triangles());
}

struct Hop {
    MoveState takeoff = MoveState::idle;
    float apex = 0;
    float speed = 0; // Horizontal, at takeoff.
};

// Runs north (or stands, with `stick` 0) and presses jump on each landing tick, holding
// it. `delay` ticks spent on the ground before each press after the first.
std::vector<Hop> hop_chain(float stick, int hops, int delay = 0, bool hold = true) {
    const auto world = floor_world();
    mj::game::Player player;
    player.place({0, 0.01F, 0}, 0);
    for (int i = 0; i < 120; ++i) // Get up to speed.
        player.tick({0, stick, 0, 0, 0}, world, k_dt);
    std::vector<Hop> result;
    int waited = 0;
    for (int tick = 0; tick < 2400 && static_cast<int>(result.size()) <= hops; ++tick) {
        const auto &s = player.current();
        std::uint32_t buttons = hold ? button::jump_held : 0U;
        if (s.grounded && ++waited > delay && static_cast<int>(result.size()) < hops) {
            buttons |= button::jump_pressed | button::jump_held;
            waited = 0;
        }
        const bool was_grounded = s.grounded;
        player.tick({0, stick, 0, 0, buttons}, world, k_dt);
        const auto &after = player.current();
        if (was_grounded && !after.grounded)
            result.push_back(
                {after.state, after.position.y, mj::length(mj::horizontal(after.velocity))});
        if (!result.empty() && !after.grounded)
            result.back().apex = std::max(result.back().apex, after.position.y);
        if (static_cast<int>(result.size()) == hops && after.grounded && !was_grounded)
            break;
    }
    return result;
}
} // namespace

TEST_CASE("jumping again as you land chains a double and then a triple jump") {
    const auto hops = hop_chain(1, 4);
    REQUIRE(hops.size() == 4);
    CHECK(hops[0].takeoff == MoveState::jump);
    CHECK(hops[1].takeoff == MoveState::double_jump);
    CHECK(hops[2].takeoff == MoveState::triple_jump);
    CHECK(hops[3].takeoff == MoveState::jump); // The chain starts over after a triple.
    // Heights follow Super Mario 64's ratios: about 1.44x and 1.9x a running jump.
    CHECK(hops[0].apex == doctest::Approx(2.63F).epsilon(0.03));
    CHECK(hops[1].apex == doctest::Approx(2.63F * 1.44F).epsilon(0.03));
    CHECK(hops[2].apex == doctest::Approx(2.63F * 1.9F).epsilon(0.03));
    CHECK(hops[3].apex == doctest::Approx(hops[0].apex).epsilon(0.01));
}

TEST_CASE("the jump chain needs quick takeoffs, and a triple jump needs speed") {
    const auto world = floor_world();
    SUBCASE("waiting past the window starts over") {
        const auto late = hop_chain(1, 3, 20); // 20 ticks is 0.17 s on the ground.
        REQUIRE(late.size() == 3);
        for (const auto &hop : late)
            CHECK(hop.takeoff == MoveState::jump);
    }
    SUBCASE("in place, the third jump is another double, not a triple") {
        const auto standing = hop_chain(0, 3);
        REQUIRE(standing.size() == 3);
        CHECK(standing[1].takeoff == MoveState::double_jump);
        CHECK(standing[2].takeoff == MoveState::jump);
    }
}

TEST_CASE("double jumps can be cut short; triple jumps keep their arc") {
    const auto held = hop_chain(1, 3, 0, true);
    const auto released = hop_chain(1, 3, 0, false);
    REQUIRE(held.size() == 3);
    REQUIRE(released.size() == 3);
    CHECK(released[1].takeoff == MoveState::double_jump);
    CHECK(released[1].apex < held[1].apex - 1.0F);
    // Releasing shortens the double jump, and the triple still follows from its landing.
    CHECK(released[2].takeoff == MoveState::triple_jump);
    CHECK(released[2].apex == doctest::Approx(held[2].apex).epsilon(0.01));
}

namespace {
struct DiveRun {
    mj::game::Player player;
    mj::CollisionWorld world = floor_world();
    void run(int ticks, std::uint32_t buttons = 0, float stick = 1) {
        for (int i = 0; i < ticks; ++i)
            player.tick({0, stick, 0, 0, buttons}, world, k_dt);
    }
    [[nodiscard]] float speed() const {
        return mj::length(mj::horizontal(player.current().velocity));
    }
};
} // namespace

TEST_CASE("a dive from a run adds speed, slides on its belly and rolls out keeping it") {
    DiveRun d;
    d.player.place({0, 0.01F, 0}, 0);
    d.run(120);
    REQUIRE(d.speed() == doctest::Approx(9.0F));
    d.run(1, button::dive_pressed);
    CHECK(d.player.current().state == MoveState::dive);
    CHECK(d.speed() == doctest::Approx(9.0F + 4.2F).epsilon(0.01));
    CHECK(d.player.current().velocity.y > 2.0F);
    // It lands on its belly and slides.
    int air = 0;
    while (!d.player.current().grounded && air++ < 120)
        d.run(1);
    REQUIRE(d.player.current().state == MoveState::dive_slide);
    const float landed = d.speed();
    CHECK(landed > 12.5F);
    // A jump press rolls out at the slide's speed, on a small fixed hop.
    d.run(1, button::jump_pressed | button::jump_held);
    CHECK(d.player.current().state == MoveState::rollout);
    CHECK(d.speed() == doctest::Approx(landed).epsilon(0.02));
    float apex = 0;
    for (int i = 0; i < 120 && !d.player.current().grounded; ++i) {
        d.run(1); // Released: the rollout's arc is fixed anyway.
        apex = std::max(apex, d.player.current().position.y);
    }
    CHECK(apex == doctest::Approx(7.5F * 7.5F / (2 * d.player.tuning.gravity())).epsilon(0.05));
    CHECK(d.player.current().state == MoveState::run);
    CHECK(d.speed() > 10.0F); // Faster than a run, so a clean hop can keep it.
}

TEST_CASE("an unbroken belly slide slows down and stands up") {
    DiveRun d;
    d.player.place({0, 0.01F, 0}, 0);
    d.run(120);
    d.run(1, button::dive_pressed);
    d.run(240, 0, 0);
    CHECK(d.player.current().grounded);
    CHECK(d.player.current().state == MoveState::idle);
    CHECK(d.speed() < 0.1F);
}

TEST_CASE("dives come from jumps and kicks once per airtime, never from long jumps") {
    SUBCASE("out of a jump, keeping its height and pushing forward") {
        DiveRun d;
        d.player.place({0, 0.01F, 0}, 0);
        d.run(120);
        d.run(1, button::jump_pressed | button::jump_held);
        d.run(20, button::jump_held);
        const float rising = d.player.current().velocity.y;
        d.run(1, button::dive_pressed | button::jump_held);
        CHECK(d.player.current().state == MoveState::dive);
        CHECK(d.speed() == doctest::Approx(13.2F).epsilon(0.02));
        CHECK(d.player.current().velocity.y < rising); // Gravity only; a dive adds no height.
        d.run(1, button::dive_pressed);
        CHECK(d.player.current().state == MoveState::dive);
        CHECK(d.speed() < 13.3F); // A second press does nothing.
    }
    SUBCASE("not out of a long jump") {
        DiveRun d;
        d.player.place({0, 0.01F, 0}, 0);
        d.run(120);
        d.run(1, button::jump_pressed | button::jump_held | button::long_jump_held);
        REQUIRE(d.player.current().state == MoveState::long_jump);
        d.run(5, button::dive_pressed);
        CHECK(d.player.current().state == MoveState::long_jump);
    }
    SUBCASE("not from a walk") {
        DiveRun d;
        d.player.place({0, 0.01F, 0}, 0);
        d.run(120, 0, 0.4F);
        d.run(1, button::dive_pressed, 0.4F);
        CHECK(d.player.current().grounded);
        CHECK(d.player.current().state == MoveState::run);
    }
    SUBCASE("momentum above the dive's top speed is kept") {
        DiveRun d;
        d.player.place({0, 0.01F, 0}, 0);
        d.run(120);
        d.run(1, button::jump_pressed | button::jump_held | button::long_jump_held);
        while (!d.player.current().grounded)
            d.run(1);
        d.run(1, button::jump_pressed | button::jump_held | button::long_jump_held);
        const float fast = d.speed();
        REQUIRE(fast > 13.5F);
        while (!d.player.current().grounded)
            d.run(1);
        d.run(1, button::dive_pressed);
        CHECK(d.player.current().state == MoveState::dive);
        CHECK(d.speed() >= fast - 0.5F);
    }
}

TEST_CASE("a jump pressed during a dive rolls out the moment it lands") {
    DiveRun d;
    d.player.place({0, 0.01F, 0}, 0);
    d.run(120);
    d.run(1, button::dive_pressed);
    int ticks = 0;
    while (d.player.current().velocity.y > -1 && ticks++ < 60)
        d.run(1);
    d.run(1, button::jump_pressed | button::jump_held); // Buffered, still airborne.
    REQUIRE_FALSE(d.player.current().grounded);
    for (int i = 0; i < 60 && d.player.current().state != MoveState::rollout; ++i)
        d.run(1, button::jump_held);
    CHECK(d.player.current().state == MoveState::rollout);
    CHECK(d.speed() > 12.5F);
}
