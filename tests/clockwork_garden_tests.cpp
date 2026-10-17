#include "route_driver.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

using mj::Vec3;
using namespace route;

namespace {
const mj::game::Level &garden() {
    static const auto level = load("clockwork-garden.json");
    return level;
}
const mj::game::LevelGeometry &garden_geometry() {
    static const auto geometry = mj::game::build_geometry(garden());
    return geometry;
}
mj::game::Simulation start(std::size_t checkpoint = 0) {
    return mj::game::Simulation(garden(), garden_geometry().collision, checkpoint);
}
// Walks out of the hedged gate lawn, round the first gear, to `target`.
void leave_spawn(mj::game::Simulation &simulation, Vec3 target) {
    walk(simulation, {0, 0, 33}, 0.6F);
    walk(simulation, {target.x < 0 ? -6.0F : 6.0F, 0, 30}, 0.6F);
    walk(simulation, target, 0.6F);
}
bool found(const mj::game::Simulation &simulation, std::string_view spark) {
    return simulation.collectible_collected(spark_index(garden(), spark));
}
std::size_t mover(std::string_view name) {
    return mover_index(garden(), name);
}
// A landing at the middle of a mover's top (its pivot, unless raised by `top`).
Landing on(std::string_view name, float top = 0, float radius = 1.0F, float max_speed = 100) {
    return {{0, top, 0}, radius, max_speed, mover(name)};
}
// A gondola passing the bottom of the wheel, beside the deck, if one is.
std::optional<std::size_t> boarding_gondola(const mj::game::Simulation &simulation) {
    for (int i = 1; i <= 6; ++i) {
        const auto index = mover("Gondola " + std::to_string(i));
        const auto at = simulation.mover_pose(index, 1).position;
        if (std::abs(at.x - 40) < 1.5F && at.y < 3.4F)
            return index;
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("the Clockwork Garden loads, and every checkpoint stands still") {
    const auto &level = garden();
    CHECK(level.title == "The Clockwork Garden");
    CHECK_FALSE(level.atmosphere.grid);
    CHECK(level.collectibles.size() == 16);
    CHECK(level.movers.size() == 22);
    CHECK(garden_geometry().collision.body_count() == level.movers.size());
    CHECK(level.movers[mover("Carousel")].sound == mj::game::MoverSound::music);
    CHECK(level.movers[mover("Minute hand")].sound == mj::game::MoverSound::tick);
    CHECK(level.movers[mover("Gondola 1")].sound == mj::game::MoverSound::none);
    for (std::size_t i = 0; i < level.checkpoints.size(); ++i) {
        CAPTURE(i);
        auto simulation = start(i);
        for (int tick = 0; tick < 240; ++tick)
            simulation.tick({}, k_dt);
        CHECK(simulation.player.current().grounded);
        CHECK_FALSE(simulation.player.current().ground_body); // Never on a mover.
        CHECK(mj::length(simulation.player.current().position - level.checkpoints[i].position) <
              0.1F);
    }
}

TEST_CASE("the garden's lawn catches every fall") {
    const auto &world = garden_geometry().collision;
    for (float x = -58; x <= 58; x += 4) {
        for (float z = -58; z <= 58; z += 4) {
            const auto ground = world.raycast({{x, 40, z}, {0, -1, 0}, 60});
            REQUIRE(ground);
            CHECK(ground->point.y > garden().kill_height + 5);
        }
    }
}

TEST_CASE("the first gear and the fountain welcome you") {
    SUBCASE("hop onto the first gear's hub") {
        auto simulation = start();
        hop(simulation, {on("First gear", 0.3F, 0.5F)});
        CHECK(found(simulation, "First tick"));
    }
    SUBCASE("the fountain's spring throws you up to its spark") {
        auto simulation = start();
        leave_spawn(simulation, {3, 0, 22});
        walk(simulation, {0, 0, 15});
        hop(simulation, {{{0, 0.6F, 12.5F}, 0.8F}}); // Into the basin.
        const Vec3 spout{0, 1.3F, 10};
        bool launched = false;
        for (int tick = 0; tick < 360 && !found(simulation, "Fountain"); ++tick) {
            std::uint32_t buttons = mj::input_button::jump_held;
            if (tick == 0) // One hop onto the spring; it does the rest.
                buttons |= mj::input_button::jump_pressed;
            // Steer back over the spout while the spring throws you up.
            simulation.tick(toward(simulation, spout, launched ? 1.0F : 0.4F, buttons), k_dt);
            launched = launched || simulation.player.current().state == mj::game::MoveState::bounce;
        }
        CHECK(launched);
        CHECK(found(simulation, "Fountain"));
    }
}

TEST_CASE("the gear stair climbs turning gears to the ferry, the tower and the dial") {
    auto simulation = start();
    leave_spawn(simulation, {-15, 0, 24});
    hop(simulation, {on("Stair gear 1"), on("Stair gear 2"), on("Stair gear 3", 0.3F, 0.5F),
                     on("Stair gear 4"), on("Stair gear 5", 0.3F, 0.5F)});
    CHECK(found(simulation, "Gear stair"));
    CHECK(found(simulation, "Mainspring"));
    hop(simulation, {{{-45, 6.4F, -12.5F}}});
    CHECK(simulation.active_checkpoint() == 1);

    // The ferry pauses beside the landing; ride it across to the tower's west ledge.
    walk(simulation, {-43.5F, 6.4F, -13});
    REQUIRE(wait_until(simulation, mover_near(mover("Tower ferry"), {-38.5F, 6.4F, -13}), 25));
    hop(simulation, {on("Tower ferry")});
    REQUIRE(wait_until(simulation, mover_near(mover("Tower ferry"), {-16, 6.4F, -30}), 25,
                       on("Tower ferry")));
    hop(simulation, {{{-9.5F, 6.4F, -34.5F}}});
    CHECK(simulation.active_checkpoint() == 2);
    walk(simulation, {-8, 6.4F, -37});
    CHECK(found(simulation, "West landing"));

    // The long lift rises to the gallery.
    walk(simulation, {-11.8F, 6.4F, -35});
    REQUIRE(wait_until(simulation, mover_near(mover("Gallery lift"), {-14, 6.4F, -35}), 25));
    hop(simulation, {on("Gallery lift", 0, 0.8F)});
    REQUIRE(wait_until(simulation, mover_near(mover("Gallery lift"), {-14, 21, -35}), 25,
                       on("Gallery lift")));
    hop(simulation, {{{-8, 21, -35}}});
    walk(simulation, {-7.5F, 21, -42.5F});
    CHECK(simulation.active_checkpoint() == 3);
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("from the gallery, a lift or a spring reaches the dial") {
    SUBCASE("the dial lift, then over the minute hand to the hour") {
        auto simulation = start(3);
        walk(simulation, {0, 21, -26.6F}, 0.4F);
        REQUIRE(wait_until(simulation, mover_near(mover("Dial lift"), {0, 21, -24.6F}), 15));
        hop(simulation, {on("Dial lift", 0, 0.8F)});
        REQUIRE(wait_until(simulation, mover_near(mover("Dial lift"), {0, 26.4F, -24.6F}), 15,
                           on("Dial lift")));
        hop(simulation, {{{0, 26, -28}, 1.0F}, {{0, 27, -35}, 0.6F}});
        CHECK(found(simulation, "The Hour"));
    }
    SUBCASE("the spring in the gallery's corner") {
        auto simulation = start(3);
        walk(simulation, {-7.5F, 21, -27.5F}, 0.6F); // Round the tower.
        walk(simulation, {6.5F, 21, -28.5F}, 0.4F);
        const Vec3 pad{7.6F, 21.22F, -27.4F};
        const Vec3 dial{3, 26, -31};
        bool launched = false;
        for (int tick = 0; tick < 600; ++tick) {
            const auto &state = simulation.player.current();
            launched = launched || state.state == mj::game::MoveState::bounce;
            if (launched && state.grounded)
                break;
            simulation.tick(launched ? aim(simulation, dial, mj::input_button::jump_held)
                                     : toward(simulation, pad, 0.4F),
                            k_dt);
        }
        CHECK(launched);
        CHECK(simulation.player.current().position.y == doctest::Approx(26).epsilon(0.01));
    }
}

TEST_CASE("the piston steps rise in turns to the brass terrace") {
    auto simulation = start();
    leave_spawn(simulation, {22, 0, 28});
    REQUIRE(wait_until(simulation, mover_near(mover("Piston 1"), {22, 0.9F, 24}), 8));
    hop(simulation, {on("Piston 1")});
    for (int i = 1; i < 4; ++i) {
        const auto from = "Piston " + std::to_string(i);
        const auto to = "Piston " + std::to_string(i + 1);
        const auto &pistons = garden().movers;
        const Vec3 top = pistons[mover(from)].pivot + Vec3{0, 3, 0};
        REQUIRE(wait_until(simulation, mover_near(mover(from), top), 8, on(from)));
        hop(simulation, {on(to)});
    }
    CHECK(found(simulation, "Piston top"));
    REQUIRE(
        wait_until(simulation, mover_near(mover("Piston 4"), {37, 8.4F, 24}), 8, on("Piston 4")));
    hop(simulation, {{{43, 9, 24}}});
    walk(simulation, {45, 9, 24});
    CHECK(simulation.active_checkpoint() == 5);
    walk(simulation, {47, 9, 20});
    CHECK(found(simulation, "Brass terrace"));
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("the ferris wheel lifts you to the sky walk and its ferry to the tower") {
    auto simulation = start(6);
    walk(simulation, {40, 3, -30.6F}, 0.4F);
    std::size_t gondola = 0;
    REQUIRE(wait_until(
        simulation,
        [&](const mj::game::Simulation &s) {
            const auto passing = boarding_gondola(s);
            gondola = passing.value_or(0);
            return passing.has_value();
        },
        35));
    hop(simulation, {{{0, 0, 0}, 1.0F, 100, gondola}});
    const Landing seat{{0, 0, 0}, 0.6F, 100, gondola};
    REQUIRE(
        wait_until(simulation, [&](const auto &s) { return found(s, "Halfway round"); }, 20, seat));
    // Hop for the spark above the top of the wheel, landing back in the gondola.
    REQUIRE(wait_until(
        simulation, [&](const auto &s) { return s.mover_pose(gondola, 1).position.y > 20.9F; }, 20,
        seat));
    // Hop straight up for the spark above the top of the wheel, and land back in the seat.
    simulation.tick({0, 0, 0, -0.32F, mj::input_button::jump_pressed | mj::input_button::jump_held},
                    k_dt);
    for (int tick = 0; tick < 240 && !simulation.player.current().grounded; ++tick)
        simulation.tick(aim(simulation, landing_point(simulation, seat),
                            mj::input_button::jump_held, landing_drift(simulation, seat)),
                        k_dt);
    CHECK(landed_on(simulation, seat));
    CHECK(found(simulation, "Top of the wheel"));
    hop(simulation, {{{39, 21, -32.3F}, 1.0F}});

    walk(simulation, {31, 21, -32});
    REQUIRE(wait_until(simulation, mover_near(mover("Sky ferry"), {27.5F, 21, -32}), 12));
    hop(simulation, {on("Sky ferry")});
    REQUIRE(wait_until(simulation, mover_near(mover("Sky ferry"), {16.5F, 21, -32}), 12,
                       on("Sky ferry")));
    CHECK(found(simulation, "Sky ferry"));
    hop(simulation, {{{11.5F, 21, -32}}});
    walk(simulation, {7.5F, 21, -32});
    walk(simulation, {7.5F, 21, -42.5F});
    CHECK(found(simulation, "Gallery"));
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("the carousel's plinths climb to the brass horse") {
    auto simulation = start();
    simulation.player.place({-22, 0.01F, -35}, -mj::k_pi / 2);
    hop(simulation, {{{2.5F, 0, -8.6F}, 1.2F, 100, mover("Carousel")}});
    hop(simulation, {{{0, 1.2F, -7}, 0.7F, 4, mover("Carousel")},
                     {{4.61F, 2.2F, -5.27F}, 0.7F, 4, mover("Carousel")},
                     {{6.97F, 3.2F, -0.89F}, 0.7F, 4, mover("Carousel")},
                     {{5.82F, 4.2F, 3.89F}, 0.7F, 4, mover("Carousel")}});
    const Landing perch{{5.82F, 4.2F, 3.89F}, 0.5F, 100, mover("Carousel")};
    CHECK(wait_until(simulation, [](const auto &s) { return found(s, "Brass horse"); }, 30, perch));
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("riding the carousel's rim passes its low spark") {
    auto simulation = start();
    simulation.player.place({-22, 0.01F, -35}, -mj::k_pi / 2);
    const Landing rim{{0, 0, -8.6F}, 0.8F, 100, mover("Carousel")};
    hop(simulation, {rim});
    CHECK(
        wait_until(simulation, [](const auto &s) { return found(s, "Round and round"); }, 30, rim));
}

TEST_CASE("secrets above and behind the clock") {
    SUBCASE("a triple jump the length of the dial reaches the weathervane, a double can't") {
        // Runs north along the dial past the hub, jumps `stages` times in a row, each the
        // moment it lands, the first `first` metres from the vane, and lines up the last.
        const Vec3 vane{2, 30.4F, -43.6F};
        const auto reaches = [&](int stages, float first) {
            auto simulation = start(4);
            simulation.player.place({2, 26.01F, -26.9F}, 0);
            int jumps = 0;
            for (int tick = 0; tick < 900; ++tick) {
                const auto &state = simulation.player.current();
                const float distance = mj::length(mj::horizontal(vane - state.position));
                std::uint32_t buttons = mj::input_button::jump_held;
                if (state.grounded && jumps > 0 && jumps < stages && state.position.y < 27) {
                    buttons |= mj::input_button::jump_pressed;
                    ++jumps;
                } else if (state.grounded && jumps == 0 && distance <= first) {
                    buttons |= mj::input_button::jump_pressed;
                    ++jumps;
                }
                const bool last = jumps == stages && !state.grounded;
                simulation.tick(last ? aim(simulation, vane, buttons)
                                     : toward(simulation, vane, 0.75F, buttons),
                                k_dt);
                trace("vane", simulation);
                const auto &after = simulation.player.current();
                if (after.grounded && std::abs(after.position.y - vane.y) < 0.15F)
                    return found(simulation, "Weathervane");
                if ((jumps == stages && after.grounded) || after.position.y < 25)
                    return false;
            }
            return false;
        };
        bool triple = false;
        bool double_jump = false;
        for (float first = 4; first <= 16; first += 0.25F) {
            triple = triple || reaches(3, first);
            double_jump = double_jump || reaches(2, first);
        }
        CHECK(triple);
        CHECK_FALSE(double_jump);
    }
    SUBCASE("drop off the gallery's north edge and steer back under it") {
        auto simulation = start(3);
        walk(simulation, {0, 21, -42.5F}, 0.4F);
        const Vec3 ledge{0, 14, -42.2F};
        for (int tick = 0; tick < 600; ++tick) {
            const auto &state = simulation.player.current();
            if (state.grounded && state.position.y < 15)
                break;
            simulation.tick(state.grounded ? toward(simulation, {0, 21, -46}, 0.25F)
                                           : aim(simulation, ledge, mj::input_button::jump_held),
                            k_dt);
            trace("drop", simulation);
        }
        CHECK(simulation.player.current().position.y == doctest::Approx(14).epsilon(0.01));
        walk(simulation, {0, 14, -42.4F}, 0.3F);
        CHECK(found(simulation, "Behind the clock"));
    }
}
