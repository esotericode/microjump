#pragma once

#include <cstdint>
#include <optional>

namespace mj {

// Display pacing (VK_KHR_present_wait, where available): before sampling input and time for
// a frame, wait until an earlier frame has reached the screen. Frames then start once per
// display refresh, so each one samples game time one refresh after the last, however the
// driver queues or recycles swapchain images.
struct DisplayPacing {
    bool enabled = true;
    // Finished frames that may wait for the display while the next one is built. 2 leaves a
    // whole refresh of headroom; 1 has a refresh less latency, but under a compositor (which
    // reports presentation late and composites early) heavy frames then miss refreshes.
    std::uint32_t queued_frames = 2;
};

// The present ID to wait for before building the next frame, given the last ID presented on
// the current swapchain (IDs start at 1). None when there is nothing to wait for yet.
[[nodiscard]] constexpr std::optional<std::uint64_t>
display_wait_target(std::uint64_t last_present, const DisplayPacing &pacing) {
    const std::uint32_t frames = pacing.queued_frames;
    const std::uint64_t queued = frames < 1 ? 1 : (frames > 2 ? 2 : frames);
    if (last_present < queued)
        return std::nullopt;
    return last_present - (queued - 1);
}

} // namespace mj
