#pragma once

#include <mj/core/error.hpp>
#include <volk.h>

#include <expected>
#include <utility>

namespace mj {
inline Error vulkan_error(const char *operation, VkResult result) {
    return {std::string(operation) + " failed (VkResult " +
            std::to_string(static_cast<int>(result)) + ")"};
}

inline std::expected<std::uint32_t, Error>
memory_type(VkPhysicalDevice physical, std::uint32_t bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1U << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    return std::unexpected(Error{"No compatible Vulkan memory type"});
}

struct BufferDescription {
    VkDeviceSize size;
    VkBufferUsageFlags usage;
};
struct ImageDescription {
    VkExtent2D extent;
    VkFormat format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
};
struct Buffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    bool coherent = false;
    Buffer() = default;
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept {
        *this = std::move(other);
    }
    Buffer &operator=(Buffer &&other) noexcept {
        if (this != &other) {
            destroy();
            device = other.device;
            handle = std::exchange(other.handle, VK_NULL_HANDLE);
            memory = std::exchange(other.memory, VK_NULL_HANDLE);
            mapped = std::exchange(other.mapped, nullptr);
            coherent = other.coherent;
        }
        return *this;
    }
    ~Buffer() {
        destroy();
    }
    void destroy() {
        if (mapped != nullptr)
            vkUnmapMemory(device, memory);
        if (handle != VK_NULL_HANDLE)
            vkDestroyBuffer(device, handle, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
        handle = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        mapped = nullptr;
    }
    std::expected<void, Error> cache(bool invalidate) const {
        if (coherent)
            return {};
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        const auto result = invalidate ? vkInvalidateMappedMemoryRanges(device, 1, &range)
                                       : vkFlushMappedMemoryRanges(device, 1, &range);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("Mapped memory cache operation", result));
        return {};
    }
    static std::expected<Buffer, Error> create(VkDevice device, VkPhysicalDevice physical,
                                               BufferDescription description) {
        Buffer buffer;
        buffer.device = device;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = description.size;
        info.usage = description.usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto result = vkCreateBuffer(device, &info, nullptr, &buffer.handle);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkCreateBuffer", result));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
        auto type =
            memory_type(physical, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buffer.coherent = type.has_value();
        if (!type)
            type = memory_type(physical, requirements.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (!type)
            return std::unexpected(type.error());
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = *type;
        result = vkAllocateMemory(device, &allocation, nullptr, &buffer.memory);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkAllocateMemory(buffer)", result));
        result = vkBindBufferMemory(device, buffer.handle, buffer.memory, 0);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkBindBufferMemory", result));
        result = vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkMapMemory", result));
        return buffer;
    }
};

struct Image {
    VkDevice device = VK_NULL_HANDLE;
    VkImage handle = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    Image() = default;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&other) noexcept {
        *this = std::move(other);
    }
    Image &operator=(Image &&other) noexcept {
        if (this != &other) {
            destroy();
            device = other.device;
            handle = std::exchange(other.handle, VK_NULL_HANDLE);
            view = std::exchange(other.view, VK_NULL_HANDLE);
            memory = std::exchange(other.memory, VK_NULL_HANDLE);
        }
        return *this;
    }
    ~Image() {
        destroy();
    }
    void destroy() {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
        if (handle != VK_NULL_HANDLE)
            vkDestroyImage(device, handle, nullptr);
        if (memory != VK_NULL_HANDLE)
            vkFreeMemory(device, memory, nullptr);
        handle = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
    static std::expected<Image, Error> create(VkDevice device, VkPhysicalDevice physical,
                                              const ImageDescription &description) {
        Image image;
        image.device = device;
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = description.format;
        info.extent = {description.extent.width, description.extent.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = description.usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto result = vkCreateImage(device, &info, nullptr, &image.handle);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkCreateImage", result));
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, image.handle, &requirements);
        const auto type =
            memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!type)
            return std::unexpected(type.error());
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = *type;
        result = vkAllocateMemory(device, &allocation, nullptr, &image.memory);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkAllocateMemory(image)", result));
        result = vkBindImageMemory(device, image.handle, image.memory, 0);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkBindImageMemory", result));
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image.handle;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = description.format;
        view_info.subresourceRange = {description.aspect, 0, 1, 0, 1};
        result = vkCreateImageView(device, &view_info, nullptr, &image.view);
        if (result != VK_SUCCESS)
            return std::unexpected(vulkan_error("vkCreateImageView", result));
        return image;
    }
};
} // namespace mj
