#include "level.hpp"

#include <array>

namespace mj::game {

Vec3 linear_color(unsigned int srgb) {
    auto channel = [](unsigned int value) {
        const float c = static_cast<float>(value & 255U) / 255.0F;
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    return {channel(srgb >> 16U), channel(srgb >> 8U), channel(srgb)};
}

LevelGeometry build_geometry(const Level &level) {
    LevelGeometry geometry;
    for (const auto &shape : level.shapes) {
        switch (shape.kind) {
        case ShapeKind::box:
            geometry.mesh.add_box({shape.center, shape.size, shape.color, shape.yaw});
            break;
        case ShapeKind::ramp:
            geometry.mesh.add_ramp({shape.center, shape.size, shape.color, shape.yaw});
            break;
        case ShapeKind::cylinder:
            geometry.mesh.add_cylinder(shape.center, shape.size.x * 0.5F, shape.size.y,
                                       shape.color);
            break;
        }
    }
    geometry.collision = CollisionWorld(geometry.mesh.triangles());
    return geometry;
}

SceneMesh make_player_mesh(float radius, float height) {
    SceneMesh mesh;
    mesh.add_capsule({{0, 0, 0}, radius, height, linear_color(0xe8704a)});
    // A visor marks the facing direction (local -Z).
    mesh.add_box({{0, height - radius * 1.25F, -radius * 0.78F},
                  {radius * 1.15F, radius * 0.42F, radius * 0.5F},
                  linear_color(0x23313a)});
    return mesh;
}

namespace {
struct YardBuilder {
    Level level;
    void box(Vec3 center, Vec3 size, unsigned int color, float yaw = 0) {
        level.shapes.push_back({ShapeKind::box, center, size, linear_color(color), yaw});
    }
    // Solid block standing on the ground (y = 0) with its top at `top`.
    void block(float x, float z, Vec3 footprint_top, unsigned int color) {
        const float top = footprint_top.y;
        box({x, top * 0.5F, z}, {footprint_top.x, top, footprint_top.z}, color);
    }
    void ramp(Vec3 center, Vec3 size, unsigned int color, float yaw = 0) {
        level.shapes.push_back({ShapeKind::ramp, center, size, linear_color(color), yaw});
    }
    void cylinder(Vec3 center, float diameter, float height, unsigned int color) {
        level.shapes.push_back(
            {ShapeKind::cylinder, center, {diameter, height, diameter}, linear_color(color)});
    }
    // A 6 cm pad; the respawn point sits just above it.
    void checkpoint(Vec3 ground, float yaw) {
        level.checkpoints.push_back({ground + Vec3{0, 0.07F, 0}, yaw});
        cylinder(ground + Vec3{0, 0.03F, 0}, 1.6F, 0.06F, 0x7fd6c8);
    }
};
} // namespace

Level make_movement_yard() {
    constexpr unsigned int k_ground = 0xc2cac6;
    constexpr unsigned int k_white = 0xe7e2d3;
    constexpr unsigned int k_teal = 0x42a5a0;
    constexpr unsigned int k_orange = 0xf1ab4a;
    constexpr unsigned int k_blue = 0x507b99;
    constexpr unsigned int k_steep = 0xc4553f;
    YardBuilder yard;
    yard.level.name = "m2-yard";
    yard.box({0, -0.5F, 0}, {100, 1, 100}, k_ground);
    yard.checkpoint({0, 0, 8}, 0);

    // Plaza centerpiece: tiers of 0.5 m, 1.0 m and 1.8 m to jump onto.
    yard.cylinder({0, 0.25F, -6}, 7, 0.5F, k_blue);
    yard.cylinder({0, 0.5F, -6}, 5.2F, 1.0F, k_white);
    yard.cylinder({0, 0.9F, -6}, 3.2F, 1.8F, k_teal);

    // Slope course: 15, 30, 45 and 58 degree ramps up to a 3 m deck. 58 exceeds max_slope.
    constexpr float k_deck = 3;
    const std::array<float, 4> angles{15, 30, 45, 58};
    for (std::size_t i = 0; i < angles.size(); ++i) {
        const float length = k_deck / std::tan(angles[i] * k_pi / 180);
        const float x = 15 + static_cast<float>(i) * 5;
        yard.ramp({x, k_deck * 0.5F, -12 + length * 0.5F}, {4, k_deck, length},
                  angles[i] > 50 ? k_steep : k_teal);
    }
    yard.box({22, k_deck * 0.5F, -15}, {20, k_deck, 6}, k_white);
    yard.checkpoint({22, k_deck, -15}, 0);

    // Stairs: 0.25 m risers (walkable) and 0.45 m risers (above step_height: jump them).
    for (int step = 0; step < 10; ++step) {
        const float s = static_cast<float>(step);
        yard.block(16, 4.225F + s * 0.45F, {3, 0.25F * (s + 1), 0.45F}, k_white);
    }
    yard.block(16, 10, {3, 2.5F, 3}, k_white);
    yard.checkpoint({16, 2.5F, 10}, k_pi);
    for (int step = 0; step < 6; ++step) {
        const float s = static_cast<float>(step);
        yard.block(21, 4.4F + s * 0.8F, {3, 0.45F * (s + 1), 0.8F}, k_orange);
    }
    // Step blocks: 0.15, 0.30 (climbable), 0.40 and 0.60 m (need a jump).
    const std::array<float, 4> steps{0.15F, 0.3F, 0.4F, 0.6F};
    for (std::size_t i = 0; i < steps.size(); ++i) {
        yard.block(27, 4 + static_cast<float>(i) * 3.5F, {2.4F, steps[i], 2.4F},
                   steps[i] <= 0.35F ? k_teal : k_orange);
    }

    // Platform field: rising route with 1.5-3.5 m gaps and one narrow precision beam.
    struct Platform {
        float x, z, top, width, depth;
    };
    const std::array<Platform, 8> platforms{{{-9, 0, 0.8F, 3, 3},
                                             {-13, -3.5F, 1.6F, 3, 3},
                                             {-17.5F, -7, 1.2F, 2.5F, 2.5F},
                                             {-17.5F, -12, 2.0F, 3, 3},
                                             {-13, -16, 2.6F, 3, 3},
                                             {-9, -19.5F, 3.2F, 2.5F, 2.5F},
                                             {-5, -22.5F, 3.8F, 1.2F, 3.5F},
                                             {-5, -29, 3.8F, 5, 5}}};
    for (std::size_t i = 0; i < platforms.size(); ++i) {
        const auto &p = platforms[i];
        yard.block(p.x, p.z, {p.width, p.top, p.depth}, i % 2 == 0 ? k_orange : k_teal);
    }
    yard.checkpoint({-5, 3.8F, -29}, k_pi);

    // Sprint lane markers every 5 m along x = 7; taller every 10 m.
    for (int i = 0; i <= 9; ++i) {
        const float z = -static_cast<float>(i) * 5;
        const float height = i % 2 == 0 ? 1.2F : 0.6F;
        for (const float x : {4.5F, 9.5F})
            yard.cylinder({x, height * 0.5F, z}, 0.3F, height, i % 2 == 0 ? k_orange : k_white);
    }

    // Walls: a long wall to slide along, a right-angle corner and an acute corner.
    yard.box({-30, 2, 15}, {1, 4, 20}, k_blue);
    yard.box({-18, 2, 12}, {8, 4, 0.8F}, k_blue);
    yard.box({-14.4F, 2, 15.6F}, {0.8F, 4, 8}, k_blue);
    // Acute corner: two walls meeting at an apex, opening toward -Z.
    const Vec3 apex{-21, 2, 27};
    for (const float yaw : {0.45F, -0.45F}) {
        const Vec3 axis{std::cos(yaw), 0, -std::sin(yaw)}; // Local +X after rotation.
        yard.box(yaw > 0 ? apex + axis * 4 : apex - axis * 4, {8, 4, 0.8F}, k_blue, yaw);
    }
    // Pillars to weave between.
    for (int i = 0; i < 5; ++i)
        yard.cylinder({-6 + static_cast<float>(i) * 3, 1.5F, 20}, 1.2F, 3, k_white);
    // Low ceiling: 2.0 m clearance for a 1.6 m body, so jumping inside bumps the roof.
    yard.box({4.5F, 1.3F, 15}, {0.6F, 2.6F, 6}, k_teal);
    yard.box({8.5F, 1.3F, 15}, {0.6F, 2.6F, 6}, k_teal);
    yard.box({6.5F, 2.3F, 15}, {4.6F, 0.6F, 6}, k_white);
    // A floating slab with an underside to hit.
    yard.box({-5, 3.2F, 9}, {4, 0.3F, 4}, k_white);
    return yard.level;
}

} // namespace mj::game
