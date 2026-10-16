#include <mj/platform/window.hpp>
#include <mj/render/device_requirements.hpp>
#include <mj/render/vulkan_context.hpp>

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace mj {
namespace {

#ifdef MJ_VULKAN_VALIDATION
constexpr const char *k_validation_layer = "VK_LAYER_KHRONOS_validation";
#endif

Error vk_error(std::string_view operation, VkResult result) {
    return Error{std::string(operation) + " failed (VkResult " +
                 std::to_string(static_cast<int>(result)) + ")"};
}

VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT,
                                                   const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                   void *user_data) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        auto &validation = *static_cast<ValidationLog *>(user_data);
        validation.error_count.fetch_add(1, std::memory_order_relaxed);
    }
    log(data->pMessage != nullptr ? data->pMessage : "Vulkan validation message without text");
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debug_info(ValidationLog *validation) {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = validation_callback;
    info.pUserData = validation;
    return info;
}

} // namespace

std::expected<std::unique_ptr<VulkanContext>, Error>
VulkanContext::create(const Window &window, ValidationLog &validation) {
    auto context = std::unique_ptr<VulkanContext>(new VulkanContext);
    context->m_validation = &validation;
    if (auto result = context->initialize(window); !result) {
        return std::unexpected(result.error());
    }
    return context;
}

std::expected<void, Error> VulkanContext::initialize(const Window &window) {
    if (const auto result = volkInitialize(); result != VK_SUCCESS) {
        return std::unexpected(vk_error("volkInitialize (install a Vulkan GPU driver)", result));
    }
    std::uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion == nullptr) {
        return std::unexpected(Error{"Vulkan loader is older than 1.1; Vulkan 1.3 is required"});
    }
    if (const auto result = vkEnumerateInstanceVersion(&version); result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkEnumerateInstanceVersion", result));
    }
    if (version < VK_API_VERSION_1_3) {
        return std::unexpected(Error{"Vulkan loader 1.3 or newer is required"});
    }

    Uint32 extension_count = 0;
    const auto *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (sdl_extensions == nullptr) {
        return std::unexpected(Error{"SDL Vulkan extensions: " + std::string(SDL_GetError())});
    }
    std::vector<const char *> extensions(sdl_extensions, sdl_extensions + extension_count);
    std::vector<const char *> layers;
#ifdef MJ_VULKAN_VALIDATION
    std::uint32_t layer_count = 0;
    if (const auto result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkEnumerateInstanceLayerProperties", result));
    }
    std::vector<VkLayerProperties> available_layers(layer_count);
    if (const auto result =
            vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkEnumerateInstanceLayerProperties", result));
    }
    if (std::ranges::none_of(available_layers, [](const auto &layer) {
            return std::strcmp(layer.layerName, k_validation_layer) == 0;
        })) {
        return std::unexpected(Error{"Debug builds require VK_LAYER_KHRONOS_validation; install "
                                     "Vulkan validation layers (or the Vulkan SDK on Windows)"});
    }
    layers.push_back(k_validation_layer);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
#endif

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "microjump";
    application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.pEngineName = "microjump";
    application.engineVersion = application.applicationVersion;
    application.apiVersion = VK_API_VERSION_1_3;
    auto debug = debug_info(m_validation);
#ifdef MJ_VULKAN_VALIDATION
    const VkValidationFeatureEnableEXT synchronization =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features{};
    validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validation_features.pNext = &debug;
    validation_features.enabledValidationFeatureCount = 1;
    validation_features.pEnabledValidationFeatures = &synchronization;
#endif
    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application;
    instance_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    instance_info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instance_info.ppEnabledLayerNames = layers.data();
    if (!layers.empty()) {
        // Also catch validation messages during instance creation/destruction.
#ifdef MJ_VULKAN_VALIDATION
        instance_info.pNext = &validation_features;
#endif
    }
    if (const auto result = vkCreateInstance(&instance_info, nullptr, &m_instance);
        result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkCreateInstance", result));
    }
    volkLoadInstance(m_instance);
    if (!layers.empty()) {
        if (const auto result =
                vkCreateDebugUtilsMessengerEXT(m_instance, &debug, nullptr, &m_debug_messenger);
            result != VK_SUCCESS) {
            return std::unexpected(vk_error("vkCreateDebugUtilsMessengerEXT", result));
        }
        log("Vulkan validation: enabled (errors fail this run)");
    } else {
        log("Vulkan validation: disabled in this release build");
    }
    if (!SDL_Vulkan_CreateSurface(window.native_handle(), m_instance, nullptr, &m_surface)) {
        return std::unexpected(Error{"SDL_Vulkan_CreateSurface: " + std::string(SDL_GetError())});
    }
    return select_device();
}

std::expected<void, Error> VulkanContext::select_device() {
    std::uint32_t count = 0;
    if (const auto result = vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
        result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkEnumeratePhysicalDevices", result));
    }
    std::vector<VkPhysicalDevice> devices(count);
    if (const auto result = vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
        result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkEnumeratePhysicalDevices", result));
    }
    for (const auto device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(device, &features);

        DeviceSupport support{};
        support.api_major = VK_API_VERSION_MAJOR(properties.apiVersion);
        support.api_minor = VK_API_VERSION_MINOR(properties.apiVersion);
        support.dynamic_rendering = features13.dynamicRendering == VK_TRUE;
        support.synchronization2 = features13.synchronization2 == VK_TRUE;
        support.timeline_semaphore = features12.timelineSemaphore == VK_TRUE;

        std::uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());
        std::uint32_t graphics_family = 0;
        for (std::uint32_t index = 0; index < queue_count; ++index) {
            VkBool32 present = VK_FALSE;
            const auto result =
                vkGetPhysicalDeviceSurfaceSupportKHR(device, index, m_surface, &present);
            if (result != VK_SUCCESS) {
                return std::unexpected(vk_error("vkGetPhysicalDeviceSurfaceSupportKHR", result));
            }
            if (queues[index].queueCount > 0 &&
                (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present == VK_TRUE) {
                graphics_family = index;
                support.graphics_and_present_queue = true;
                break;
            }
        }

        std::uint32_t extension_count = 0;
        auto result =
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
        if (result != VK_SUCCESS) {
            return std::unexpected(vk_error("vkEnumerateDeviceExtensionProperties", result));
        }
        std::vector<VkExtensionProperties> extensions(extension_count);
        result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                                      extensions.data());
        if (result != VK_SUCCESS) {
            return std::unexpected(vk_error("vkEnumerateDeviceExtensionProperties", result));
        }
        const auto has_extension = [&extensions](const char *name) {
            return std::ranges::any_of(extensions, [name](const auto &extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        };
        support.swapchain = has_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        std::uint32_t format_count = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, nullptr);
        if (result != VK_SUCCESS) {
            return std::unexpected(vk_error("vkGetPhysicalDeviceSurfaceFormatsKHR", result));
        }
        support.surface_formats = format_count > 0;
        std::uint32_t mode_count = 0;
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &mode_count, nullptr);
        if (result != VK_SUCCESS) {
            return std::unexpected(vk_error("vkGetPhysicalDeviceSurfacePresentModesKHR", result));
        }
        support.present_modes = mode_count > 0;
        if (const auto reason = device_rejection_reason(support); !reason.empty()) {
            log(std::string("Skipping ") + properties.deviceName + ": " + std::string(reason));
            continue;
        }
        m_physical_device = device;
        m_queue_family = graphics_family;
        m_present_wait = has_extension(VK_KHR_PRESENT_ID_EXTENSION_NAME) &&
                         has_extension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        log(std::string("Vulkan device: ") + properties.deviceName);
        break;
    }
    if (m_physical_device == VK_NULL_HANDLE) {
        return std::unexpected(
            Error{"No Vulkan 1.3 device supports presentation, dynamic rendering, "
                  "synchronization2 and timeline semaphores"});
    }

    // Optional: present IDs and waits let the renderer pace frames to the display.
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait{};
    present_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
    VkPhysicalDevicePresentIdFeaturesKHR present_id{};
    present_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
    present_id.pNext = &present_wait;
    if (m_present_wait) {
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &present_id;
        vkGetPhysicalDeviceFeatures2(m_physical_device, &features);
        m_present_wait = present_id.presentId == VK_TRUE && present_wait.presentWait == VK_TRUE;
    }

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = m_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    if (m_present_wait)
        features13.pNext = &present_id; // present_id chains present_wait.
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    features12.timelineSemaphore = VK_TRUE;
    std::vector<const char *> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (m_present_wait) {
        device_extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    }
    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = &features12;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions.size());
    device_info.ppEnabledExtensionNames = device_extensions.data();
    if (const auto result = vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device);
        result != VK_SUCCESS) {
        return std::unexpected(vk_error("vkCreateDevice", result));
    }
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_graphics_queue);
    log("Vulkan 1.3 device ready: dynamic rendering, synchronization2, timeline semaphores "
        "enabled");
    log(m_present_wait ? "Present wait: available; frames are paced to the display"
                       : "Present wait: unavailable; frames are paced by swapchain image "
                         "availability");
    return {};
}

VulkanContext::~VulkanContext() {
    if (m_device != VK_NULL_HANDLE) {
        if (const auto result = vkDeviceWaitIdle(m_device); result != VK_SUCCESS) {
            log(vk_error("vkDeviceWaitIdle during shutdown", result).message);
            m_validation->error_count.fetch_add(1, std::memory_order_relaxed);
        }
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_debug_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

} // namespace mj
