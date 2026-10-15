#pragma once
#include <mj/scene/mesh.hpp>

namespace mj::game {
inline Vec3 linear_color(unsigned int rgb) {
    auto channel = [](unsigned int value) {
        const float c = static_cast<float>(value & 255U) / 255.0F;
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    return {channel(rgb >> 16U), channel(rgb >> 8U), channel(rgb)};
}

inline SceneMesh make_scene() {
    SceneMesh mesh;
    const auto white = linear_color(0xe7e2d3);
    const auto teal = linear_color(0x42a5a0);
    const auto orange = linear_color(0xf1ab4a);
    const auto blue = linear_color(0x507b99);
    mesh.add_box({{0, -0.5F, 0}, {32, 1, 32}, blue});
    for (int x = -8; x < 8; ++x) {
        for (int z = -8; z < 8; ++z) {
            const auto color = linear_color((x + z) % 2 == 0 ? 0xc9ceca : 0xb3c0bf);
            mesh.add_box({{static_cast<float>(x) * 2 + 1, 0.015F, static_cast<float>(z) * 2 + 1},
                          {1.97F, 0.03F, 1.97F},
                          color});
        }
    }
    mesh.add_cylinder({0, 0.25F, 0}, 3.5F, 0.5F, blue);
    mesh.add_cylinder({0, 0.65F, 0}, 2.8F, 0.3F, white);
    mesh.add_cylinder({0, 1.15F, 0}, 1.8F, 0.7F, teal);
    mesh.add_box({{0, 2.5F, 0}, {1.7F, 2, 1.7F}, orange, 0.55F});
    mesh.add_box({{0, 3.6F, 0}, {1.85F, 0.2F, 1.85F}, white, 0.55F});
    for (int step = 0; step < 6; ++step) {
        const float s = static_cast<float>(step);
        mesh.add_box({{5 + s * 1.15F, 0.3F + s * 0.35F, -1 - s * 1.3F},
                      {2.6F, 0.6F + s * 0.7F, 2.6F},
                      teal});
        mesh.add_box(
            {{5 + s * 1.15F, 0.65F + s * 0.7F, -1 - s * 1.3F}, {2.7F, 0.12F, 2.7F}, white});
    }
    for (const float x : {-10.0F, -4.0F}) {
        mesh.add_box({{x, 3, -7}, {1.3F, 6, 1.3F}, orange});
        mesh.add_box({{x, 0.3F, -7}, {2, 0.6F, 2}, white});
    }
    mesh.add_box({{-7, 6.2F, -7}, {8, 0.7F, 1.6F}, orange});
    mesh.add_box({{-7, 6.65F, -7}, {8.2F, 0.2F, 1.8F}, white});
    for (int i = 0; i < 4; ++i) {
        const float n = static_cast<float>(i);
        mesh.add_cylinder({-9 + n * 3, 1.0F + n * 0.35F, 8}, 0.85F, 2 + n * 0.7F, blue);
        mesh.add_cylinder({-9 + n * 3, 2.1F + n * 0.7F, 8}, 1, 0.2F, orange);
    }
    for (int i = 0; i < 5; ++i) {
        const float n = static_cast<float>(i);
        mesh.add_box({{-6, 0.15F + n * 0.15F, 2 + n * 0.8F}, {3, 0.3F + n * 0.3F, 0.8F}, teal});
    }
    return mesh;
}
} // namespace mj::game
