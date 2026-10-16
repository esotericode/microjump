#include "test_framework.hpp"

#include <spark_visuals.hpp>

#include <mj/core/fixed_step.hpp>
#include <mj/core/launch_options.hpp>
#include <mj/core/png.hpp>
#include <mj/scene/debug_camera.hpp>
#include <mj/scene/mesh.hpp>

#include <cmath>
#include <fstream>
#include <limits>

TEST_CASE("reverse-Z projection follows Vulkan near/far and handedness conventions") {
    const auto projection =
        mj::perspective_reverse_z({.vertical_fov = 1.0F, .aspect = 2.0F, .near_plane = 0.1F});
    const auto near = mj::transform(projection, {0, 0, -0.1F, 1});
    const auto far = mj::transform(projection, {0, 0, -10000.0F, 1});
    CHECK(near[2] / near[3] == doctest::Approx(1));
    CHECK(far[2] / far[3] == doctest::Approx(0.00001F));
    CHECK(mj::transform(projection, {0, 1, -2, 1})[1] < 0.0F);
    const auto view = mj::view_matrix({2, 3, 4}, 0, 0);
    const auto target = mj::transform(view, {2, 3, 2, 1});
    CHECK(target[0] == doctest::Approx(0));
    CHECK(target[1] == doctest::Approx(0));
    CHECK(target[2] == doctest::Approx(-2));
    const auto combined = mj::transform(projection * view, {2, 3, 2, 1});
    CHECK(combined[2] / combined[3] == doctest::Approx(0.05F));
}

TEST_CASE("fixed camera simulation does not depend on render cadence") {
    for (const int rate : {30, 60, 144, 240}) {
        mj::DebugCamera camera;
        mj::FixedStep step;
        const auto start = camera.current();
        int ticks = 0;
        for (int frame = 0; frame < rate * 2; ++frame) {
            const float alpha = step.advance(1.0 / rate, [&](float dt) {
                ++ticks;
                camera.tick({1, 0, 0, false}, dt);
            });
            CHECK(alpha >= 0.0F);
            CHECK(alpha <= 1.0F);
        }
        CHECK(ticks == 240);
        const auto delta = camera.current().position - start.position;
        CHECK(std::sqrt(mj::dot(delta, delta)) == doctest::Approx(14).epsilon(0.0001));
    }
    mj::DebugCamera whole, split;
    whole.look(120, -50);
    for (int i = 0; i < 10; ++i)
        split.look(12, -5);
    CHECK(whole.current().yaw == doctest::Approx(split.current().yaw));
    CHECK(whole.current().pitch == doctest::Approx(split.current().pitch));
    whole.look(0, -100000);
    CHECK(whole.current().pitch == doctest::Approx(1.5F));
    whole.reset();
    CHECK(whole.current().position.x == doctest::Approx(14));
}

TEST_CASE("fixed-step catch-up is bounded and invalid clock samples are ignored") {
    mj::FixedStep step;
    int ticks = 0;
    auto tick = [&](float) { ++ticks; };
    step.advance(std::numeric_limits<double>::quiet_NaN(), tick);
    step.advance(-1, tick);
    CHECK(ticks == 0);
    step.advance(10, tick);
    CHECK(ticks == 30);
}

TEST_CASE("generated primitives have outward unit normals and consistent winding") {
    mj::SceneMesh mesh;
    mesh.add_box({{0, 0, 0}, {2, 4, 6}, {1, 1, 1}, 0.4F});
    CHECK(mesh.vertices.size() == std::size_t{36});
    mesh.add_cylinder({0, 0, 0}, 1, 2, {1, 1, 1});
    CHECK(mesh.vertices.size() == std::size_t{36 + 32 * 12});
    for (std::size_t index = 0; index < mesh.vertices.size(); index += 3) {
        const auto &a = mesh.vertices[index];
        const auto &b = mesh.vertices[index + 1];
        const auto &c = mesh.vertices[index + 2];
        const auto winding =
            mj::normalized(mj::cross(b.position - a.position, c.position - a.position));
        CHECK(mj::dot(winding, a.normal) > 0.99F);
        CHECK(mj::dot(a.normal, a.normal) == doctest::Approx(1));
        CHECK(mj::dot(a.position, a.normal) > 0.0F);
    }
}

TEST_CASE("ellipsoids and cones close around their boxes with outward-facing triangles") {
    const mj::Vec3 center{1, 2, 3};
    const mj::Vec3 size{4, 2, 3};
    for (const int kind : {0, 1, 2, 3}) {
        CAPTURE(kind);
        mj::SceneMesh mesh;
        if (kind == 0)
            mesh.add_ellipsoid({center, size, {1, 1, 1}, 0.7F});
        else
            mesh.add_cone({center, size, {1, 1, 1}, 0.3F, kind == 2 ? 0.0F : 0.4F, kind == 3});
        REQUIRE_FALSE(mesh.vertices.empty());
        REQUIRE(mesh.vertices.size() % 3 == 0);
        float volume = 0; // Divergence theorem: a closed, outward mesh encloses positive volume.
        for (std::size_t i = 0; i < mesh.vertices.size(); i += 3) {
            const auto &a = mesh.vertices[i];
            const auto &b = mesh.vertices[i + 1];
            const auto &c = mesh.vertices[i + 2];
            const auto winding =
                mj::normalized(mj::cross(b.position - a.position, c.position - a.position));
            // Smooth ellipsoid normals lean from the facet; cone facets are flat.
            CHECK(mj::dot(winding, a.normal) > (kind == 0 ? 0.7F : 0.99F));
            CHECK(mj::length(a.normal) == doctest::Approx(1));
            CHECK(mj::dot(winding, (a.position + b.position + c.position) * (1.0F / 3) - center) >
                  0.0F);
            volume +=
                mj::dot(a.position - center, mj::cross(b.position - center, c.position - center)) /
                6;
            for (const auto *vertex : {&a, &b, &c}) {
                const auto local = vertex->position - center;
                CHECK(std::abs(local.y) <= size.y * 0.5F + 1e-4F);
                CHECK(std::hypot(local.x, local.z) <= size.x * 0.5F + 1e-4F);
            }
        }
        const float box = size.x * size.y * size.z;
        if (kind == 0)
            CHECK(volume == doctest::Approx(box * mj::k_pi / 6).epsilon(0.05)); // Ellipsoid.
        else
            CHECK(volume > box * 0.12F); // Cones fill about a quarter of their box, or more.
        CHECK(volume < box);
    }
}

TEST_CASE("spark facets face outward and stay inside the centered visual envelope") {
    const auto mesh = mj::game::make_spark_mesh();
    REQUIRE(mesh.vertices.size() == 24);
    for (std::size_t i = 0; i < mesh.vertices.size(); i += 3) {
        const auto &a = mesh.vertices[i];
        const auto &b = mesh.vertices[i + 1];
        const auto &c = mesh.vertices[i + 2];
        const auto normal =
            mj::normalized(mj::cross(b.position - a.position, c.position - a.position));
        CHECK(mj::dot(normal, a.normal) > 0.99F);
        CHECK(mj::dot(a.position, normal) > 0.0F);
        for (const auto *vertex : {&a, &b, &c}) {
            CHECK(mj::length(vertex->position) <= 0.48F);
            CHECK(mj::length(vertex->normal) == doctest::Approx(1));
        }
    }
}

TEST_CASE("capture CLI rejects incomplete, impossible and overflowing requests") {
    auto parse = [](std::initializer_list<std::string_view> args) {
        return mj::parse_launch_options({args.begin(), args.size()});
    };
    CHECK_FALSE(parse({"--frames", "0"}));
    CHECK_FALSE(parse({"--frames", "-1"}));
    CHECK_FALSE(parse({"--frames", "1000001"}));
    CHECK_FALSE(parse({"--frames", "999999999999999999999"}));
    CHECK_FALSE(parse({"--frames", "2junk"}));
    CHECK_FALSE(parse({"--frames"}));
    CHECK_FALSE(parse({"--capture-frame", "4"}));
    CHECK_FALSE(parse({"--capture-out", "frame.png"}));
    CHECK_FALSE(parse({"--frames", "2", "--capture-frame", "4", "--capture-out", "frame.png"}));
    CHECK_FALSE(parse({"--frames", "2", "--frames", "3"}));
    const auto result = parse({"--capture-frame", "4", "--capture-out", "frame.png"});
    REQUIRE(result);
    CHECK(result->capture_frame == 4);
    CHECK(result->capture_out == "frame.png");
    CHECK(parse({"--smoke-test"})->frames == 12);
    CHECK(parse({"--travel-test"})->frames == 16);
    CHECK_FALSE(parse({"--travel-test", "--replay", "input.mjr"})); // Gates rest in replays.
}

TEST_CASE("PNG writer validates dimensions and reports filesystem failures") {
    const std::array<std::uint8_t, 4> pixel{255, 0, 0, 255};
    CHECK_FALSE(mj::write_png("unused.png", 0, 1, pixel));
    CHECK_FALSE(mj::write_png("unused.png", 2, 1, pixel));
    const std::filesystem::path blocker("png-path-blocker");
    {
        std::ofstream file(blocker);
        file << "not a directory";
    }
    CHECK_FALSE(mj::write_png(blocker / "bad.png", 1, 1, pixel));
    std::error_code error;
    std::filesystem::remove(blocker, error);
}
