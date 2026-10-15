#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace mj {

struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalized(Vec3 v) {
    const float length = std::sqrt(dot(v, v));
    return length > 0 ? v * (1.0F / length) : Vec3{};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

// Column-major, column vectors. GLSL consumes this array without transposition.
struct Mat4 {
    std::array<float, 16> values{};
    float &at(std::size_t row, std::size_t column) {
        return values[column * 4 + row];
    }
    float at(std::size_t row, std::size_t column) const {
        return values[column * 4 + row];
    }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 result;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t k = 0; k < 4; ++k) {
                result.at(row, column) += a.at(row, k) * b.at(k, column);
            }
        }
    }
    return result;
}

inline std::array<float, 4> transform(const Mat4 &matrix, std::array<float, 4> v) {
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result[row] += matrix.at(row, column) * v[column];
        }
    }
    return result;
}

// RH view space (-Z forward), Vulkan Y/depth, infinite far plane, near -> 1, far -> 0.
struct Perspective {
    float vertical_fov;
    float aspect;
    float near_plane;
};
inline Mat4 perspective_reverse_z(Perspective perspective) {
    const float f = 1.0F / std::tan(perspective.vertical_fov * 0.5F);
    Mat4 result;
    result.at(0, 0) = f / perspective.aspect;
    result.at(1, 1) = -f;
    result.at(2, 3) = perspective.near_plane;
    result.at(3, 2) = -1;
    return result;
}

inline Mat4 view_matrix(Vec3 position, float yaw, float pitch) {
    const Vec3 forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                       -std::cos(yaw) * std::cos(pitch)};
    const Vec3 right{std::cos(yaw), 0, std::sin(yaw)};
    const Vec3 up = cross(right, forward);
    Mat4 result;
    const std::array axes{right, up, forward * -1.0F};
    for (std::size_t row = 0; row < 3; ++row) {
        result.at(row, 0) = axes[row].x;
        result.at(row, 1) = axes[row].y;
        result.at(row, 2) = axes[row].z;
        result.at(row, 3) = -dot(axes[row], position);
    }
    result.at(3, 3) = 1;
    return result;
}

} // namespace mj
