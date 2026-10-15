#pragma once

#include <mj/core/error.hpp>

#include <volk.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>

namespace mj {

class Window;

// Caller owns this through context teardown, so destruction errors also fail the run.
struct ValidationLog {
    std::atomic<std::uint32_t> error_count = 0;
};

struct VulkanDevice {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue queue;
    std::uint32_t queue_family;
};

class VulkanContext {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<VulkanContext>, Error>
    create(const Window &window, ValidationLog &validation);
    ~VulkanContext();
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;
    [[nodiscard]] VulkanDevice device() const {
        return {m_instance, m_physical_device, m_device,
                m_surface,  m_graphics_queue,  m_queue_family};
    }

  private:
    VulkanContext() = default;
    [[nodiscard]] std::expected<void, Error> initialize(const Window &window);
    [[nodiscard]] std::expected<void, Error> select_device();

    ValidationLog *m_validation = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphics_queue = VK_NULL_HANDLE;
    std::uint32_t m_queue_family = 0;
};

} // namespace mj
