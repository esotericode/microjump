#pragma once

#include <algorithm>
#include <cmath>

namespace mj {

class FixedStep {
  public:
    static constexpr double k_tick = 1.0 / 120.0;

    template <typename Tick> float advance(double elapsed, Tick tick) {
        if (std::isfinite(elapsed)) {
            m_accumulator += std::clamp(elapsed, 0.0, 0.25);
        }
        while (m_accumulator + 1.0e-10 >= k_tick) {
            tick(static_cast<float>(k_tick));
            m_accumulator -= k_tick;
        }
        return static_cast<float>(std::clamp(m_accumulator / k_tick, 0.0, 1.0));
    }

  private:
    double m_accumulator = 0;
};

} // namespace mj
