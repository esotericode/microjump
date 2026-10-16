#include "level.hpp"

#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace mj::game {

std::string_view zone_at(const Level &level, Vec3 position) {
    std::string_view name = "Connections";
    float closest = std::numeric_limits<float>::max();
    for (const auto &zone : level.zones) {
        const float distance = length_squared(horizontal(position - zone.center));
        if (distance <= zone.radius * zone.radius && distance < closest) {
            closest = distance;
            name = zone.name;
        }
    }
    return name;
}

Vec3 linear_color(unsigned int srgb) {
    auto channel = [](unsigned int value) {
        const float c = static_cast<float>(value & 255U) / 255.0F;
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    return {channel(srgb >> 16U), channel(srgb >> 8U), channel(srgb)};
}

LevelGeometry build_geometry(const Level &level) {
    LevelGeometry geometry;
    std::vector<bool> wall_flags;
    std::vector<bool> solid_flags;
    for (const auto &shape : level.shapes) {
        geometry.mesh.surface = shape.surface;
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
        case ShapeKind::ellipsoid:
            geometry.mesh.add_ellipsoid({shape.center, shape.size, shape.color, shape.yaw});
            break;
        case ShapeKind::cone:
            geometry.mesh.add_cone(
                {shape.center, shape.size, shape.color, shape.yaw, shape.taper, shape.tip_down});
            break;
        }
        wall_flags.resize(geometry.mesh.vertices.size() / 3, shape.wall_jump);
        solid_flags.resize(geometry.mesh.vertices.size() / 3, shape.solid);
    }
    std::vector<Triangle> triangles;
    const auto drawn = geometry.mesh.triangles();
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        if (!solid_flags[i])
            continue;
        triangles.push_back(drawn[i]);
        triangles.back().wall_jump = wall_flags[i];
    }
    geometry.collision = CollisionWorld(triangles);
    return geometry;
}

SceneMesh make_player_mesh(float radius, float height) {
    SceneMesh mesh;
    mesh.surface = {0.35F, 0}; // Glossy plastic.
    mesh.add_capsule({{0, 0, 0}, radius, height, linear_color(0xe8704a)});
    // A visor marks the facing direction (local -Z).
    mesh.surface = {0.15F, 0};
    mesh.add_box({{0, height - radius * 1.25F, -radius * 0.78F},
                  {radius * 1.15F, radius * 0.42F, radius * 0.5F},
                  linear_color(0x23313a)});
    return mesh;
}

SceneMesh make_bulb_mesh() {
    SceneMesh mesh;
    mesh.add_capsule({{0, -0.12F, 0}, 0.12F, 0.24F, {1, 1, 1}});
    return mesh;
}

namespace {
struct YardBuilder {
    Level level;
    std::optional<Surface> override_surface; // Replaces the palette's default while set.

    // Default material response per palette color, so light reads differently on each.
    [[nodiscard]] Surface surface_for(unsigned int color) const {
        if (override_surface)
            return *override_surface;
        switch (color) {
        case 0xc2cac6: // Ground: matte.
            return {0.85F, 0};
        case 0x42a5a0: // Teal: satin paint.
            return {0.45F, 0};
        case 0x507b99: // Blue walls: semi-gloss.
            return {0.4F, 0};
        default:
            return {0.6F, 0};
        }
    }
    void push(ShapeKind kind, const Shape &shape) {
        Shape placed = shape;
        placed.kind = kind;
        level.shapes.push_back(placed);
    }
    void box(Vec3 center, Vec3 size, unsigned int color, float yaw = 0) {
        push(ShapeKind::box,
             {ShapeKind::box, center, size, linear_color(color), yaw, surface_for(color)});
    }
    // Solid block standing on the ground (y = 0) with its top at `top`.
    void block(float x, float z, Vec3 footprint_top, unsigned int color) {
        const float top = footprint_top.y;
        box({x, top * 0.5F, z}, {footprint_top.x, top, footprint_top.z}, color);
    }
    void ramp(Vec3 center, Vec3 size, unsigned int color, float yaw = 0) {
        push(ShapeKind::ramp,
             {ShapeKind::ramp, center, size, linear_color(color), yaw, surface_for(color)});
    }
    void cylinder(Vec3 center, float diameter, float height, unsigned int color) {
        push(ShapeKind::cylinder, {ShapeKind::cylinder,
                                   center,
                                   {diameter, height, diameter},
                                   linear_color(color),
                                   0,
                                   surface_for(color)});
    }
    // A street lamp that switches on at dusk.
    void lamp(float x, float z) {
        override_surface = Surface{0.35F, 1};
        cylinder({x, 1.6F, z}, 0.18F, 3.2F, 0x3a3f44);
        override_surface.reset();
        LightFixture light;
        light.position = {x, 3.35F, z};
        light.color = linear_color(0xffc890);
        light.intensity = 18;
        light.range = 11;
        light.night_only = true;
        level.lights.push_back(light);
    }
    // A 6 cm pad; the respawn point sits just above it.
    void checkpoint(Vec3 ground, float yaw) {
        level.checkpoints.push_back({ground + Vec3{0, 0.07F, 0}, yaw});
        cylinder(ground + Vec3{0, 0.03F, 0}, 1.6F, 0.06F, 0x7fd6c8);
    }
};
// Lighting showcase: a roofed pavilion walled on its sunny sides, so it stays in shade. Inside:
// a polished dark floor, brushed-steel pillars, dielectric samples from glossy to matte, metal
// samples, a warm fixed light, a spot light and two orbiting colored lights.
void add_lighting_pavilion(YardBuilder &yard) {
    constexpr float k_x = 30;
    constexpr float k_z = 30;
    yard.override_surface = Surface{0.12F, 0};
    yard.box({k_x, 0.15F, k_z}, {12, 0.3F, 12}, 0x2b3036);
    yard.override_surface = Surface{0.3F, 1};
    for (const float dx : {-5.5F, 5.5F}) {
        for (const float dz : {-5.5F, 5.5F})
            yard.cylinder({k_x + dx, 2.3F, k_z + dz}, 0.7F, 4, 0xc8ccd0);
    }
    yard.override_surface.reset();
    yard.box({k_x, 4.5F, k_z}, {13, 0.4F, 13}, 0xe7e2d3);
    yard.box({k_x, 2.3F, k_z + 6.2F}, {12, 4, 0.4F}, 0x8a97a3);
    yard.box({k_x + 6.2F, 2.3F, k_z}, {0.4F, 4, 12}, 0x8a97a3);
    // Dielectric roughness ramp on a shelf: 0.05, 0.25, 0.45, 0.7, 0.95.
    yard.box({k_x, 0.65F, k_z + 3.5F}, {10, 0.7F, 1.4F}, 0x5c636b);
    const std::array<float, 5> roughness{0.05F, 0.25F, 0.45F, 0.7F, 0.95F};
    for (std::size_t i = 0; i < roughness.size(); ++i) {
        yard.override_surface = Surface{roughness[i], 0};
        yard.box({k_x - 3.6F + static_cast<float>(i) * 1.8F, 1.4F, k_z + 3.5F}, {0.8F, 0.8F, 0.8F},
                 0xe7e2d3);
    }
    // Metals: gold, copper, steel.
    const std::array<std::pair<unsigned int, float>, 3> metals{
        {{0xffc35aU, 0.25F}, {0xf09a6bU, 0.4F}, {0xd0d4d8U, 0.12F}}};
    for (std::size_t i = 0; i < metals.size(); ++i) {
        yard.override_surface = Surface{metals[i].second, 1};
        yard.cylinder({k_x - 2.5F + static_cast<float>(i) * 2.5F, 0.9F, k_z - 2.5F}, 0.9F, 1.2F,
                      metals[i].first);
    }
    yard.override_surface.reset();
    yard.checkpoint({k_x - 3.5F, 0.3F, k_z - 4.5F}, k_pi);

    LightFixture warm;
    warm.position = {k_x - 3.5F, 3.4F, k_z + 1.5F};
    warm.color = linear_color(0xff9f55);
    warm.intensity = 30;
    warm.range = 10;
    LightFixture spot;
    spot.position = {k_x, 4.15F, k_z + 1.8F};
    spot.direction = normalized(Vec3{0, -1, 0.55F});
    spot.color = linear_color(0xfff2dc);
    spot.intensity = 90;
    spot.range = 9;
    spot.inner_cone = 0.35F;
    spot.outer_cone = 0.55F;
    LightFixture cyan;
    cyan.position = {k_x, 2.0F, k_z};
    cyan.color = linear_color(0x4cc8ff);
    cyan.intensity = 24;
    cyan.range = 8;
    cyan.orbit_radius = 3.2F;
    cyan.orbit_speed = 0.7F;
    LightFixture magenta = cyan;
    magenta.position = {k_x, 1.1F, k_z - 1};
    magenta.color = linear_color(0xff4fb8);
    magenta.intensity = 14;
    magenta.range = 6;
    magenta.orbit_radius = 2;
    magenta.orbit_speed = -1.1F;
    yard.level.lights.insert(yard.level.lights.end(), {warm, spot, cyan, magenta});
}
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

    add_lighting_pavilion(yard);
    for (const auto &[x, z] : std::array<std::pair<float, float>, 6>{{{7.0F, 10.0F},
                                                                      {-9.0F, 10.0F},
                                                                      {11.0F, -20.0F},
                                                                      {13.0F, 8.0F},
                                                                      {-12.0F, 4.0F},
                                                                      {24.0F, 22.0F}}})
        yard.lamp(x, z);
    return yard.level;
}

} // namespace mj::game
