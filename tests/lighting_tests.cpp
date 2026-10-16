#include "test_framework.hpp"

#include <level.hpp>
#include <lighting.hpp>

#include <mj/core/launch_options.hpp>
#include <mj/render/lighting.hpp>

#include <array>
#include <cmath>
#include <initializer_list>

namespace {
std::array<float, 3> project(const mj::Mat4 &matrix, mj::Vec3 point) {
    const auto clip = mj::transform(matrix, {point.x, point.y, point.z, 1});
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
}
} // namespace

TEST_CASE("matrix inverse, look-at and orthographic projection follow the conventions") {
    const auto perspective =
        mj::perspective_reverse_z({.vertical_fov = 1.0F, .aspect = 1.6F, .near_plane = 0.1F}) *
        mj::view_matrix({3, 2, -5}, 0.7F, -0.3F);
    const auto product = perspective * mj::inverse(perspective);
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column)
            CHECK(product.at(row, column) == doctest::Approx(row == column ? 1 : 0).epsilon(1e-4));
    }
    const auto view = mj::look_at({{1, 2, 3}, {1, 2, -7}});
    const auto eye = mj::transform(view, {1, 2, 3, 1});
    const auto ahead = mj::transform(view, {1, 2, -7, 1});
    CHECK(eye[2] == doctest::Approx(0));
    CHECK(ahead[0] == doctest::Approx(0));
    CHECK(ahead[2] == doctest::Approx(-10)); // Right-handed: forward is -Z in view space.
    const auto down = mj::look_at({{0, 10, 0}, {0, 0, 0}}); // Degenerate up vector handled.
    CHECK(mj::transform(down, {0, 0, 0, 1})[2] == doctest::Approx(-10));

    const auto box = mj::orthographic({4, 2, 1, 21});
    const auto near = mj::transform(box, {4, 2, -1, 1});
    const auto far = mj::transform(box, {-4, -2, -21, 1});
    CHECK(near[0] == doctest::Approx(1));
    CHECK(near[1] == doctest::Approx(1));
    CHECK(near[2] == doctest::Approx(0));
    CHECK(far[0] == doctest::Approx(-1));
    CHECK(far[2] == doctest::Approx(1));
}

TEST_CASE("sun shadow fit covers the focus area and snaps to whole texels") {
    const mj::Vec3 sun = mj::normalized({-0.6F, 0.65F, 0.45F});
    for (const auto quality :
         {mj::ShadowQuality::low, mj::ShadowQuality::medium, mj::ShadowQuality::high}) {
        CAPTURE(mj::to_string(quality));
        mj::ShadowSettings settings;
        settings.quality = quality;
        settings.focus = {12.3F, 1.7F, -4.2F};
        const auto preset = mj::shadow_preset(quality);
        const auto fit = mj::fit_shadow(sun, settings);
        CHECK(fit.texel_size ==
              doctest::Approx(2 * preset.radius / static_cast<float>(preset.resolution)));
        // The focus, and casters well above it, are inside the map with usable depth.
        for (const mj::Vec3 offset : {mj::Vec3{0, 0, 0}, mj::Vec3{0, 30, 0},
                                      mj::Vec3{preset.radius * 0.6F, 0, -preset.radius * 0.6F}}) {
            const auto p = project(fit.view_projection, settings.focus + offset);
            CHECK(std::abs(p[0]) < 1.0F);
            CHECK(std::abs(p[1]) < 1.0F);
            CHECK(p[2] > 0.0F);
            CHECK(p[2] < 1.0F);
        }
        // Points nearer the light have smaller depth (standard 0 = near).
        const auto ground = project(fit.view_projection, settings.focus);
        const auto above = project(fit.view_projection, settings.focus + sun * 5.0F);
        CHECK(above[2] < ground[2]);

        // Moving the focus shifts the projection by whole texels only, so static shadow
        // edges do not crawl while the player moves.
        const mj::Vec3 landmark{20, 0, 3};
        const auto before = project(fit.view_projection, landmark);
        for (const float step : {0.013F, 0.37F, 1.9F}) {
            auto moved = settings;
            moved.focus += mj::Vec3{step, 0.2F * step, -0.7F * step};
            const auto after = project(mj::fit_shadow(sun, moved).view_projection, landmark);
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const float texels =
                    (after[axis] - before[axis]) * 0.5F * static_cast<float>(preset.resolution);
                CHECK(std::abs(texels - std::round(texels)) < 0.02F);
            }
        }
    }
}

TEST_CASE("day/night cycle: sun by day, moon by night, smooth and exposure-compensated") {
    using mj::game::DayCycle;
    CHECK(mj::game::daylight(12) == doctest::Approx(1));
    CHECK(mj::game::daylight(0) == doctest::Approx(0));
    const auto noon = mj::game::environment_at(DayCycle{12});
    const auto midnight = mj::game::environment_at(DayCycle{0});
    const auto dusk = mj::game::environment_at(DayCycle{17.8F});
    CHECK(noon.sun_direction.y > 0.7F);
    CHECK(midnight.sun_direction.y > 0.7F); // The moon takes over the shadowed light.
    auto luminance = [](mj::Vec3 c) { return 0.2126F * c.x + 0.7152F * c.y + 0.0722F * c.z; };
    CHECK(luminance(noon.sun_color) > 10 * luminance(midnight.sun_color));
    CHECK(luminance(noon.sky_zenith) > 10 * luminance(midnight.sky_zenith));
    CHECK(dusk.sun_color.x > dusk.sun_color.z * 1.5F); // Warm low sun.
    CHECK(midnight.exposure > noon.exposure * 2);
    // The light never dips below the horizon (shadow maps need a usable angle) and no
    // quantity jumps between 1-minute steps, including through sunrise and sunset.
    auto previous = mj::game::environment_at(DayCycle{0});
    for (int minute = 1; minute <= 24 * 60; ++minute) {
        const auto current = mj::game::environment_at(DayCycle{static_cast<float>(minute) / 60});
        CHECK(current.sun_direction.y >= 0.11F);
        CHECK(mj::length(current.sun_color - previous.sun_color) < 0.25F);
        CHECK(mj::length(current.sky_horizon - previous.sky_horizon) < 0.02F);
        CHECK(std::abs(current.exposure - previous.exposure) < 0.05F);
        previous = current;
    }
    DayCycle cycle{23.9F, true, 240};
    cycle.advance(2); // 0.2 h later wraps past midnight.
    CHECK(cycle.hours == doctest::Approx(0.1F).epsilon(0.01));
}

TEST_CASE("light fixtures: orbits move at their radius and street lamps light only at night") {
    const auto yard = mj::game::make_movement_yard();
    REQUIRE(yard.lights.size() <= mj::k_max_local_lights);
    std::size_t night_only = 0;
    const mj::game::LightFixture *orbiting = nullptr;
    for (const auto &fixture : yard.lights) {
        night_only += fixture.night_only ? 1U : 0U;
        if (fixture.orbit_radius > 0)
            orbiting = &fixture;
    }
    REQUIRE(orbiting != nullptr);
    CHECK(night_only >= 4);
    const auto day = mj::game::animate_lights(yard.lights, {0, 12});
    const auto night = mj::game::animate_lights(yard.lights, {0, 0});
    CHECK(day.size() == yard.lights.size() - night_only);
    CHECK(night.size() == yard.lights.size());

    const mj::game::LightFixture single = *orbiting;
    const auto start = mj::game::animate_lights({&single, 1}, {0, 12}).front().position;
    const auto later = mj::game::animate_lights({&single, 1}, {1.5F, 12}).front().position;
    CHECK(mj::length(start - later) > 0.5F);
    CHECK(mj::length(mj::horizontal(later - single.position)) ==
          doctest::Approx(single.orbit_radius));
}

TEST_CASE("level surfaces carry material response into the rendered vertices") {
    const auto geometry = mj::game::build_geometry(mj::game::make_movement_yard());
    float smoothest = 1;
    bool metal = false;
    for (const auto &vertex : geometry.mesh.vertices) {
        smoothest = std::min(smoothest, vertex.surface.roughness);
        metal = metal || vertex.surface.metallic > 0.5F;
        REQUIRE(vertex.surface.roughness >= 0.0F);
        REQUIRE(vertex.surface.roughness <= 1.0F);
    }
    CHECK(smoothest < 0.1F); // Polished pavilion floor / glossy sample.
    CHECK(metal);
}

TEST_CASE("lighting command line options are validated") {
    auto parse = [](std::initializer_list<std::string_view> args) {
        return mj::parse_launch_options({args.begin(), args.size()});
    };
    CHECK(parse({"--time-of-day", "21.5"})->time_of_day == doctest::Approx(21.5F));
    CHECK(parse({"--shadow-quality", "high"})->shadow_quality == mj::ShadowQuality::high);
    CHECK(parse({"--shadow-quality", "low"})->shadow_quality == mj::ShadowQuality::low);
    CHECK(parse({"--hide-overlay"})->hide_overlay);
    CHECK_FALSE(parse({"--time-of-day"}));
    CHECK_FALSE(parse({"--time-of-day", "25"}));
    CHECK_FALSE(parse({"--time-of-day", "-1"}));
    CHECK_FALSE(parse({"--time-of-day", "noon"}));
    CHECK_FALSE(parse({"--shadow-quality", "ultra"}));
    CHECK_FALSE(parse({"--shadow-quality", "High"}));
    CHECK_FALSE(parse({"--shadow-quality", "low", "--shadow-quality", "high"}));
}
