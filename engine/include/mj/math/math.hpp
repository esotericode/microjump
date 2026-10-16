#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace mj {

inline constexpr float k_pi = 3.14159265358979F;
inline constexpr float k_tau = 6.28318530717959F;

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
inline Vec3 operator-(Vec3 v) {
    return {-v.x, -v.y, -v.z};
}
inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline Vec3 operator*(float s, Vec3 a) {
    return a * s;
}
inline Vec3 &operator+=(Vec3 &a, Vec3 b) {
    a = a + b;
    return a;
}
inline Vec3 &operator-=(Vec3 &a, Vec3 b) {
    a = a - b;
    return a;
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length_squared(Vec3 v) {
    return dot(v, v);
}
inline float length(Vec3 v) {
    return std::sqrt(dot(v, v));
}
inline Vec3 normalized(Vec3 v) {
    const float length = std::sqrt(dot(v, v));
    return length > 0 ? v * (1.0F / length) : Vec3{};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}
inline Vec3 component_min(Vec3 a, Vec3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec3 component_max(Vec3 a, Vec3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
inline Vec3 horizontal(Vec3 v) {
    return {v.x, 0, v.z};
}

// Yaw 0 faces -Z; positive yaw turns toward +X. Matches the camera convention.
inline Vec3 yaw_forward(float yaw) {
    return {std::sin(yaw), 0, -std::cos(yaw)};
}
inline Vec3 yaw_right(float yaw) {
    return {std::cos(yaw), 0, std::sin(yaw)};
}
inline float yaw_of(Vec3 direction) {
    return std::atan2(direction.x, -direction.z);
}
// Shortest signed difference to - from, in [-pi, pi].
inline float angle_difference(float from, float to) {
    return std::remainder(to - from, k_tau);
}
inline float lerp_angle(float from, float to, float t) {
    return from + angle_difference(from, to) * t;
}
// Moves value toward target by at most step.
inline float approach(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

struct Aabb {
    Vec3 min;
    Vec3 max;
};
inline Aabb merged(const Aabb &a, const Aabb &b) {
    return {component_min(a.min, b.min), component_max(a.max, b.max)};
}
inline bool overlaps(const Aabb &a, const Aabb &b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
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

// Rotation about +Y that maps local forward (-Z) to yaw_forward(yaw), then translation.
inline Mat4 yaw_translation(float yaw, Vec3 position) {
    Mat4 result;
    const Vec3 right = yaw_right(yaw);
    const Vec3 back = -yaw_forward(yaw);
    for (std::size_t row = 0; row < 3; ++row) {
        const std::array right_values{right.x, right.y, right.z};
        const std::array back_values{back.x, back.y, back.z};
        result.at(row, 0) = right_values[row];
        result.at(row, 2) = back_values[row];
    }
    result.at(1, 1) = 1;
    result.at(0, 3) = position.x;
    result.at(1, 3) = position.y;
    result.at(2, 3) = position.z;
    result.at(3, 3) = 1;
    return result;
}

// General inverse by cofactors; returns all zeros for a singular matrix.
inline Mat4 inverse(const Mat4 &m) {
    const auto &a = m.values;
    std::array<float, 16> inv{};
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
             a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
             a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
             a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
              a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
             a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
             a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
             a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
              a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
             a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
              a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
              a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
             a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const float determinant = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    Mat4 result;
    if (std::abs(determinant) < 1.0e-30F)
        return result;
    for (std::size_t i = 0; i < 16; ++i)
        result.values[i] = inv[i] / determinant;
    return result;
}

// Right-handed view looking from `eye` toward `target`.
struct LookAt {
    Vec3 eye;
    Vec3 target;
    Vec3 up{0, 1, 0};
};
inline Mat4 look_at(const LookAt &view) {
    const Vec3 forward = normalized(view.target - view.eye);
    Vec3 right = cross(forward, view.up);
    if (length_squared(right) < 1.0e-8F) // Looking along `up`: pick any perpendicular.
        right = cross(forward, std::abs(forward.x) < 0.9F ? Vec3{1, 0, 0} : Vec3{0, 0, 1});
    right = normalized(right);
    const Vec3 up = cross(right, forward);
    Mat4 result;
    const std::array axes{right, up, -forward};
    for (std::size_t row = 0; row < 3; ++row) {
        result.at(row, 0) = axes[row].x;
        result.at(row, 1) = axes[row].y;
        result.at(row, 2) = axes[row].z;
        result.at(row, 3) = -dot(axes[row], view.eye);
    }
    result.at(3, 3) = 1;
    return result;
}

// Orthographic box in RH view space; Vulkan clip space with depth 0 at `near_plane`.
struct Orthographic {
    float half_width;
    float half_height;
    float near_plane;
    float far_plane;
};
inline Mat4 orthographic(const Orthographic &box) {
    Mat4 result;
    result.at(0, 0) = 1 / box.half_width;
    result.at(1, 1) = 1 / box.half_height;
    result.at(2, 2) = -1 / (box.far_plane - box.near_plane);
    result.at(2, 3) = -box.near_plane / (box.far_plane - box.near_plane);
    result.at(3, 3) = 1;
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

inline Vec3 view_forward(float yaw, float pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

inline Mat4 view_matrix(Vec3 position, float yaw, float pitch) {
    const Vec3 forward = view_forward(yaw, pitch);
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
