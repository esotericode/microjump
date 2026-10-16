#pragma once

#include <cstddef>
#include <span>

namespace mj {

// Paired presentation: with a FIFO swapchain, frames reacquire the image presented on the
// previous frame while their intervals alternate between near zero and about two refreshes.
// The frame rate still averages the refresh rate, but two consecutive frames sample almost
// the same time, so motion judders at half rate. Observed on an NVIDIA Windows driver after
// vkDeviceWaitIdle and at startup; recreating the swapchain restores one frame per refresh.
struct PresentationWindow {
    std::span<const double> intervals; // Milliseconds between consecutive draws.
    std::span<const bool> repeats;     // Acquired index equals the previously presented one.
};
[[nodiscard]] bool paired_presentation(const PresentationWindow &window);

} // namespace mj
