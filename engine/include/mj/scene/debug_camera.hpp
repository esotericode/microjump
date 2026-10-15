#pragma once

#include <mj/math/math.hpp>

#include <algorithm>

namespace mj {

struct CameraInput {
    float strafe = 0;
    float forward = 0;
    float vertical = 0;
    bool fast = false;
};

struct CameraPose {
    Vec3 position{14, 10, 18};
    float yaw = -0.66F;
    float pitch = -0.36F;
};

struct CameraTuning {
    float speed = 7;
    float fast_multiplier = 3;
    float mouse_sensitivity = 0.003F;
};

class DebugCamera {
  public:
    CameraTuning tuning;

    void reset() {
        m_current = {};
        m_previous = m_current;
    }
    void look(float dx, float dy) {
        m_current.yaw = std::remainder(m_current.yaw + dx * tuning.mouse_sensitivity, 6.2831853F);
        m_current.pitch = std::clamp(m_current.pitch - dy * tuning.mouse_sensitivity, -1.5F, 1.5F);
    }
    void tick(CameraInput input, float dt) {
        m_previous = m_current;
        const Vec3 right{std::cos(m_current.yaw), 0, std::sin(m_current.yaw)};
        const Vec3 forward{std::sin(m_current.yaw) * std::cos(m_current.pitch),
                           std::sin(m_current.pitch),
                           -std::cos(m_current.yaw) * std::cos(m_current.pitch)};
        const Vec3 direction =
            normalized(right * input.strafe + forward * input.forward + Vec3{0, input.vertical, 0});
        const float speed = tuning.speed * (input.fast ? tuning.fast_multiplier : 1);
        m_current.position = m_current.position + direction * (speed * dt);
    }
    [[nodiscard]] CameraPose interpolated(float alpha) const {
        auto pose = m_current;
        pose.position = lerp(m_previous.position, m_current.position, alpha);
        return pose;
    }
    [[nodiscard]] const CameraPose &current() const {
        return m_current;
    }

  private:
    CameraPose m_current;
    CameraPose m_previous;
};

} // namespace mj
