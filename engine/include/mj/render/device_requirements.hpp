#pragma once

#include <cstdint>
#include <string_view>

namespace mj {

struct DeviceSupport {
    std::uint32_t api_major = 0;
    std::uint32_t api_minor = 0;
    bool graphics_and_present_queue = false;
    bool swapchain = false;
    bool surface_formats = false;
    bool present_modes = false;
    bool dynamic_rendering = false;
    bool synchronization2 = false;
    bool timeline_semaphore = false;
};

// Empty means compatible. Used by actual selection and hardware-independent tests.
[[nodiscard]] inline std::string_view device_rejection_reason(const DeviceSupport &support) {
    if (support.api_major < 1 || (support.api_major == 1 && support.api_minor < 3)) {
        return "Vulkan 1.3 is required";
    }
    if (!support.graphics_and_present_queue) {
        return "a combined graphics/presentation queue is required";
    }
    if (!support.swapchain || !support.surface_formats || !support.present_modes) {
        return "swapchain presentation support is required";
    }
    if (!support.dynamic_rendering || !support.synchronization2 || !support.timeline_semaphore) {
        return "dynamic rendering, synchronization2 and timeline semaphores are required";
    }
    return {};
}

} // namespace mj
