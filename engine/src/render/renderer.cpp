#include <mj/core/png.hpp>
#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include "resources.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

#define MJ_VK_TRY(expression)                                                                      \
    do {                                                                                           \
        const auto mj_result = (expression);                                                       \
        if (mj_result != VK_SUCCESS)                                                               \
            return std::unexpected(vulkan_error(#expression, mj_result));                          \
    } while (false)

namespace mj {
namespace {
constexpr VkFormat k_color_format = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkFormat k_depth_format = VK_FORMAT_D32_SFLOAT;
constexpr std::size_t k_frame_count = 2;

// std140 mirror of shaders' Frame block.
struct FrameUniforms {
    Mat4 view_projection;
    std::array<float, 4> sun_direction;
    std::array<float, 4> blob_shadow; // xyz caster feet, w radius.
    std::array<float, 4> camera_position;
};
// Mirror of shaders' Draw push constants: rigid model rows plus material parameters.
struct DrawConstants {
    std::array<std::array<float, 4>, 3> model_rows;
    std::array<float, 4> material; // x grid strength, y receives blob shadow.
};
static_assert(sizeof(DrawConstants) == 64);

struct ImageTransition {
    VkImageLayout old_layout;
    VkImageLayout new_layout;
    VkPipelineStageFlags2 source_stage;
    VkAccessFlags2 source_access;
    VkPipelineStageFlags2 destination_stage;
    VkAccessFlags2 destination_access;
};
void image_barrier(VkCommandBuffer command, VkImage image, VkImageAspectFlags aspect,
                   const ImageTransition &transition) {
    const auto &[old_layout, new_layout, source_stage, source_access, destination_stage,
                 destination_access] = transition;
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

std::expected<VkShaderModule, Error> load_shader(VkDevice device, const char *name) {
    const auto path = Window::executable_directory() / "shaders" / name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected(Error{"Cannot open packaged shader: " + std::string(name)});
    const auto length = file.tellg();
    if (length <= 0 || length % 4 != 0 || length > std::streamoff{1'048'576}) {
        return std::unexpected(Error{"Invalid SPIR-V shader size"});
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(length) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(words.data()), length);
    if (!file || words[0] != 0x07230203U)
        return std::unexpected(Error{"Invalid SPIR-V shader"});
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    MJ_VK_TRY(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}
} // namespace

struct Renderer::Impl {
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::uint64_t completion = 0;
        Image color;
        Image depth;
        Buffer readback;
        Buffer uniforms;
        VkDescriptorSet descriptors = VK_NULL_HANDLE;
    };
    struct MeshRange {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };
    VulkanDevice vk{};
    const Window *window = nullptr;
    std::array<Frame, k_frame_count> frames;
    Buffer vertices;
    std::vector<MeshRange> meshes;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    std::uint64_t submission = 0;
    std::size_t frame_index = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swap_images;
    std::vector<VkSemaphore> presented;
    VkExtent2D extent{};
    PixelSize requested_size{};
    VkFormat swap_format = VK_FORMAT_UNDEFINED;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool imgui_context = false;
    bool imgui_platform = false;
    bool imgui_renderer = false;
    bool recreate = false;
    double smoothed_seconds = 1.0 / 60;

    ~Impl();
    std::expected<void, Error> initialize(std::span<const SceneMesh> scene_meshes);
    std::expected<void, Error> create_descriptors();
    std::expected<void, Error> create_pipeline();
    std::expected<void, Error> create_swapchain(PixelSize size);
    std::expected<void, Error> initialize_overlay();
    std::expected<void, Error> wait(std::uint64_t value) const;
    void destroy_swapchain();
    void overlay(const FrameView &view);
    std::expected<bool, Error> draw(const FrameView &view);
};

std::expected<void, Error> Renderer::Impl::wait(std::uint64_t value) const {
    if (value == 0)
        return {};
    VkSemaphoreWaitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &timeline;
    wait_info.pValues = &value;
    MJ_VK_TRY(vkWaitSemaphores(vk.device, &wait_info, 10'000'000'000ULL));
    return {};
}

std::expected<void, Error> Renderer::Impl::initialize(std::span<const SceneMesh> scene_meshes) {
    std::size_t total = 0;
    for (const auto &mesh : scene_meshes) {
        if (mesh.vertices.empty())
            return std::unexpected(Error{"Scene mesh is empty"});
        meshes.push_back(
            {static_cast<std::uint32_t>(total), static_cast<std::uint32_t>(mesh.vertices.size())});
        total += mesh.vertices.size();
    }
    if (total == 0 || total > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(Error{"Scene meshes are empty or too large"});
    VkFormatProperties color_properties{}, depth_properties{};
    vkGetPhysicalDeviceFormatProperties(vk.physical, k_color_format, &color_properties);
    vkGetPhysicalDeviceFormatProperties(vk.physical, k_depth_format, &depth_properties);
    if ((color_properties.optimalTilingFeatures &
         (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT)) !=
            (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        (depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
            0) {
        return std::unexpected(Error{"GPU lacks required sRGB color / D32 depth formats"});
    }
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = vk.queue_family;
    MJ_VK_TRY(vkCreateCommandPool(vk.device, &pool_info, nullptr, &command_pool));
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = &type;
    MJ_VK_TRY(vkCreateSemaphore(vk.device, &semaphore_info, nullptr, &timeline));
    semaphore_info.pNext = nullptr;
    for (auto &frame : frames) {
        MJ_VK_TRY(vkCreateSemaphore(vk.device, &semaphore_info, nullptr, &frame.acquired));
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = command_pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        MJ_VK_TRY(vkAllocateCommandBuffers(vk.device, &allocation, &frame.command));
    }
    auto buffer = Buffer::create(
        vk.device, vk.physical,
        {.size = total * sizeof(Vertex), .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT});
    if (!buffer)
        return std::unexpected(buffer.error());
    vertices = std::move(*buffer);
    for (std::size_t i = 0; i < scene_meshes.size(); ++i) {
        std::memcpy(static_cast<Vertex *>(vertices.mapped) + meshes[i].first,
                    scene_meshes[i].vertices.data(),
                    scene_meshes[i].vertices.size() * sizeof(Vertex));
    }
    if (auto result = vertices.cache(false); !result)
        return result;
    if (auto result = create_descriptors(); !result)
        return result;
    if (auto result = create_pipeline(); !result)
        return result;
    const auto size = window->pixel_size();
    if (size.width == 0 || size.height == 0)
        return std::unexpected(Error{"Initial window has no drawable area"});
    if (auto result = create_swapchain(size); !result)
        return result;
    return initialize_overlay();
}

std::expected<void, Error> Renderer::Impl::create_descriptors() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    MJ_VK_TRY(vkCreateDescriptorSetLayout(vk.device, &layout_info, nullptr, &descriptor_layout));
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_frame_count};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = k_frame_count;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    MJ_VK_TRY(vkCreateDescriptorPool(vk.device, &pool_info, nullptr, &descriptor_pool));
    for (auto &frame : frames) {
        auto uniforms = Buffer::create(
            vk.device, vk.physical,
            {.size = sizeof(FrameUniforms), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT});
        if (!uniforms)
            return std::unexpected(uniforms.error());
        frame.uniforms = std::move(*uniforms);
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = descriptor_pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptor_layout;
        MJ_VK_TRY(vkAllocateDescriptorSets(vk.device, &allocation, &frame.descriptors));
        VkDescriptorBufferInfo buffer_info{frame.uniforms.handle, 0, sizeof(FrameUniforms)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.descriptors;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
    }
    return {};
}

std::expected<void, Error> Renderer::Impl::create_pipeline() {
    auto vertex = load_shader(vk.device, "scene.vert.spv");
    if (!vertex)
        return std::unexpected(vertex.error());
    auto fragment = load_shader(vk.device, "scene.frag.spv");
    if (!fragment) {
        vkDestroyShaderModule(vk.device, *vertex, nullptr);
        return std::unexpected(fragment.error());
    }
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (auto &stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = *vertex;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = *fragment;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                             sizeof(DrawConstants)};
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    const auto layout_result =
        vkCreatePipelineLayout(vk.device, &layout_info, nullptr, &pipeline_layout);
    if (layout_result != VK_SUCCESS) {
        vkDestroyShaderModule(vk.device, *vertex, nullptr);
        vkDestroyShaderModule(vk.device, *fragment, nullptr);
        return std::unexpected(vulkan_error("vkCreatePipelineLayout", layout_result));
    }
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 3> attributes{
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
         {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
         {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}}};
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT; // Generated primitives are closed and CCW.
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &k_color_format;
    rendering.depthAttachmentFormat = k_depth_format;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline_layout;
    const auto result =
        vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(vk.device, *vertex, nullptr);
    vkDestroyShaderModule(vk.device, *fragment, nullptr);
    if (result != VK_SUCCESS)
        return std::unexpected(vulkan_error("vkCreateGraphicsPipelines", result));
    return {};
}

void Renderer::Impl::destroy_swapchain() {
    for (auto &frame : frames) {
        frame.color = {};
        frame.depth = {};
        frame.readback = {};
    }
    for (const auto semaphore : presented) {
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(vk.device, semaphore, nullptr);
    }
    presented.clear();
    swap_images.clear();
    if (swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(vk.device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

std::expected<void, Error> Renderer::Impl::create_swapchain(PixelSize size) {
    destroy_swapchain();
    VkSurfaceCapabilitiesKHR capabilities{};
    MJ_VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities));
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        return std::unexpected(Error{"Surface does not support transfer-destination presentation"});
    }
    std::uint32_t format_count = 0;
    MJ_VK_TRY(
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &format_count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    MJ_VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &format_count,
                                                   formats.data()));
    auto selected = std::ranges::find_if(formats, [](const auto &format) {
        return (format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == k_color_format) &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (selected == formats.end())
        return std::unexpected(Error{"Surface has no supported sRGB format"});
    swap_format = selected->format;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(vk.physical, swap_format, &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
        return std::unexpected(Error{"Surface format does not support the presentation blit"});
    }
    extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        extent.width = std::clamp(size.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(size.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
        return std::unexpected(Error{"Surface has no drawable extent"});
    std::uint32_t count = std::max(2U, capabilities.minImageCount + 1);
    if (capabilities.maxImageCount != 0)
        count = std::min(count, capabilities.maxImageCount);
    if (count < 2)
        return std::unexpected(Error{"Surface cannot provide two presentation images"});
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (const auto candidate :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if ((capabilities.supportedCompositeAlpha &
             static_cast<VkCompositeAlphaFlagsKHR>(candidate)) != 0) {
            alpha = candidate;
            break;
        }
    }
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = vk.surface;
    info.minImageCount = count;
    info.imageFormat = swap_format;
    info.imageColorSpace = selected->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    MJ_VK_TRY(vkCreateSwapchainKHR(vk.device, &info, nullptr, &swapchain));
    MJ_VK_TRY(vkGetSwapchainImagesKHR(vk.device, swapchain, &count, nullptr));
    swap_images.resize(count);
    MJ_VK_TRY(vkGetSwapchainImagesKHR(vk.device, swapchain, &count, swap_images.data()));
    presented.resize(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto &semaphore : presented)
        MJ_VK_TRY(vkCreateSemaphore(vk.device, &semaphore_info, nullptr, &semaphore));
    for (auto &frame : frames) {
        auto color = Image::create(
            vk.device, vk.physical,
            {.extent = extent,
             .format = k_color_format,
             .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
             .aspect = VK_IMAGE_ASPECT_COLOR_BIT});
        if (!color)
            return std::unexpected(color.error());
        frame.color = std::move(*color);
        auto depth = Image::create(vk.device, vk.physical,
                                   {.extent = extent,
                                    .format = k_depth_format,
                                    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                    .aspect = VK_IMAGE_ASPECT_DEPTH_BIT});
        if (!depth)
            return std::unexpected(depth.error());
        frame.depth = std::move(*depth);
    }
    requested_size = size;
    recreate = false;
    log("Render targets: " + std::to_string(extent.width) + "x" + std::to_string(extent.height));
    return {};
}

std::expected<void, Error> Renderer::Impl::initialize_overlay() {
    if (!imgui_context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imgui_context = true;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::StyleColorsDark();
        auto &style = ImGui::GetStyle();
        style.WindowRounding = 6;
        style.WindowPadding = ImVec2(14, 12);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.008F, 0.018F, 0.025F, 0.94F);
    }
    if (!imgui_platform) {
        if (!ImGui_ImplSDL3_InitForVulkan(window->native_handle()))
            return std::unexpected(Error{"ImGui SDL initialization failed"});
        imgui_platform = true;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = vk.instance;
    info.PhysicalDevice = vk.physical;
    info.Device = vk.device;
    info.QueueFamily = vk.queue_family;
    info.Queue = vk.queue;
    info.DescriptorPoolSize = 8;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<std::uint32_t>(swap_images.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &k_color_format;
    info.PipelineRenderingCreateInfo.depthAttachmentFormat = k_depth_format;
    info.CheckVkResultFn = [](VkResult result) {
        if (result < 0) {
            log(vulkan_error("ImGui Vulkan backend", result).message);
            std::abort();
        }
    };
    if (!ImGui_ImplVulkan_Init(&info))
        return std::unexpected(Error{"ImGui Vulkan initialization failed"});
    imgui_renderer = true;
    return {};
}

void Renderer::Impl::overlay(const FrameView &view) {
    ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94F);
    constexpr auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
                           ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Diagnostics", nullptr, flags);
    ImGui::TextColored(ImVec4(0.3F, 0.85F, 0.77F, 1), "MICROJUMP");
    ImGui::Spacing();
    ImGui::Text("%.1f FPS   |   %.2f ms", 1.0 / smoothed_seconds, smoothed_seconds * 1000);
    ImGui::Text("%u x %u   VSync: On (FIFO)", extent.width, extent.height);
    ImGui::TextUnformatted("FPS cap: Uncapped   |   Tick: 120 Hz");
    if (view.overlay_lines != nullptr && *view.overlay_lines) {
        ImGui::Separator();
        (*view.overlay_lines)();
    }
    ImGui::End();
}

std::expected<bool, Error> Renderer::Impl::draw(const FrameView &view) {
    const auto *capture = view.capture;
    if (std::isfinite(view.elapsed) && view.elapsed > 0)
        smoothed_seconds += (view.elapsed - smoothed_seconds) * 0.08;
    const auto size = window->pixel_size();
    if (size.width == 0 || size.height == 0)
        return false;
    if (recreate || size.width != requested_size.width || size.height != requested_size.height) {
        MJ_VK_TRY(vkDeviceWaitIdle(vk.device));
        if (imgui_renderer) {
            ImGui_ImplVulkan_Shutdown();
            imgui_renderer = false;
        }
        if (auto result = create_swapchain(size); !result)
            return std::unexpected(result.error());
        if (auto result = initialize_overlay(); !result)
            return std::unexpected(result.error());
    }
    auto &frame = frames[frame_index];
    if (auto result = wait(frame.completion); !result)
        return std::unexpected(result.error());
    if (capture != nullptr && frame.readback.handle == VK_NULL_HANDLE) {
        auto buffer =
            Buffer::create(vk.device, vk.physical,
                           {.size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4,
                            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT});
        if (!buffer)
            return std::unexpected(buffer.error());
        frame.readback = std::move(*buffer);
    }
    std::uint32_t image_index = 0;
    const auto acquire = vkAcquireNextImageKHR(vk.device, swapchain, 1'000'000'000ULL,
                                               frame.acquired, VK_NULL_HANDLE, &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate = true;
        return false;
    }
    if (acquire == VK_TIMEOUT || acquire == VK_NOT_READY)
        return false;
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        return std::unexpected(vulkan_error("vkAcquireNextImageKHR", acquire));
    }
    recreate = acquire == VK_SUBOPTIMAL_KHR;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (view.overlay)
        overlay(view);
    if (view.debug_windows != nullptr && *view.debug_windows)
        (*view.debug_windows)();
    ImGui::Render();

    const auto &camera = view.camera;
    FrameUniforms uniforms{};
    uniforms.view_projection = perspective_reverse_z({.vertical_fov = 1.04719755F,
                                                      .aspect = static_cast<float>(extent.width) /
                                                                static_cast<float>(extent.height),
                                                      .near_plane = 0.1F}) *
                               view_matrix(camera.position, camera.yaw, camera.pitch);
    const Vec3 sun = normalized(Vec3{-0.4F, 0.85F, 0.3F});
    uniforms.sun_direction = {sun.x, sun.y, sun.z, 0};
    uniforms.blob_shadow = {view.blob.caster.x, view.blob.caster.y, view.blob.caster.z,
                            view.blob.radius};
    uniforms.camera_position = {camera.position.x, camera.position.y, camera.position.z, 1};
    std::memcpy(frame.uniforms.mapped, &uniforms, sizeof(uniforms));
    if (auto flushed = frame.uniforms.cache(false); !flushed)
        return std::unexpected(flushed.error());

    const auto command = frame.command;
    MJ_VK_TRY(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    MJ_VK_TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, frame.color.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_NONE,
                   .source_access = 0,
                   .destination_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   .destination_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});
    image_barrier(command, frame.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .new_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_NONE,
                   .source_access = 0,
                   .destination_stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   .destination_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT});
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = frame.color.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.38F, 0.49F, 0.57F, 1}};
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = frame.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {0, 0};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(command, &rendering);
    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height),
                        0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
                            &frame.descriptors, 0, nullptr);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &offset);
    for (const auto &instance : view.instances) {
        if (instance.mesh.index >= meshes.size())
            continue;
        DrawConstants constants{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 4; ++column)
                constants.model_rows[row][column] = instance.model.at(row, column);
        }
        constants.material = {instance.grid, instance.receives_blob ? 1.0F : 0.0F, 0, 0};
        vkCmdPushConstants(command, pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        const auto &range = meshes[instance.mesh.index];
        vkCmdDraw(command, range.count, 1, range.first, 0);
    }
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    vkCmdEndRendering(command);
    image_barrier(command, frame.color.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   .new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   .source_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   .destination_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   .destination_access = VK_ACCESS_2_TRANSFER_READ_BIT});
    image_barrier(command, swap_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   .source_access = 0,
                   .destination_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   .destination_access = VK_ACCESS_2_TRANSFER_WRITE_BIT});
    VkImageBlit2 region{};
    region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = region.srcSubresource;
    region.srcOffsets[1] = {static_cast<int>(extent.width), static_cast<int>(extent.height), 1};
    region.dstOffsets[1] = region.srcOffsets[1];
    VkBlitImageInfo2 blit{};
    blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blit.srcImage = frame.color.handle;
    blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit.dstImage = swap_images[image_index];
    blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit.regionCount = 1;
    blit.pRegions = &region;
    blit.filter = VK_FILTER_NEAREST;
    vkCmdBlitImage2(command, &blit);
    if (capture != nullptr) {
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(command, frame.color.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               frame.readback.handle, 1, &copy);
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = frame.readback.handle;
        barrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    }
    image_barrier(command, swap_images[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   .new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   .source_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                   .source_access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   .destination_stage = VK_PIPELINE_STAGE_2_NONE,
                   .destination_access = 0});
    MJ_VK_TRY(vkEndCommandBuffer(command));
    VkSemaphoreSubmitInfo acquired{};
    acquired.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    acquired.semaphore = frame.acquired;
    acquired.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    std::array<VkSemaphoreSubmitInfo, 2> signals{};
    for (auto &signal : signals) {
        signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    // A present semaphore belongs to its swapchain image, never its CPU frame slot.
    signals[0].semaphore = presented[image_index];
    signals[1].semaphore = timeline;
    signals[1].value = ++submission;
    VkCommandBufferSubmitInfo command_info{};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_info.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &acquired;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command_info;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos = signals.data();
    MJ_VK_TRY(vkQueueSubmit2(vk.queue, 1, &submit, VK_NULL_HANDLE));
    frame.completion = submission;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presented[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index;
    const auto result = vkQueuePresentKHR(vk.queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        recreate = true;
    else if (result != VK_SUCCESS)
        return std::unexpected(vulkan_error("vkQueuePresentKHR", result));
    if (capture != nullptr) {
        if (auto ready = wait(frame.completion); !ready)
            return std::unexpected(ready.error());
        if (auto cached = frame.readback.cache(true); !cached)
            return std::unexpected(cached.error());
        const auto bytes = static_cast<std::size_t>(extent.width) * extent.height * 4;
        if (auto saved =
                write_png(*capture, extent.width, extent.height,
                          {static_cast<const std::uint8_t *>(frame.readback.mapped), bytes});
            !saved)
            return std::unexpected(saved.error());
        log("GPU frame captured as PNG");
    }
    frame_index = (frame_index + 1) % k_frame_count;
    return true;
}

Renderer::Impl::~Impl() {
    if (vk.device == VK_NULL_HANDLE)
        return;
    if (const auto result = vkDeviceWaitIdle(vk.device); result != VK_SUCCESS) {
        log(vulkan_error("Renderer shutdown", result).message);
    }
    if (imgui_renderer)
        ImGui_ImplVulkan_Shutdown();
    if (imgui_platform)
        ImGui_ImplSDL3_Shutdown();
    if (imgui_context)
        ImGui::DestroyContext();
    destroy_swapchain();
    for (const auto &frame : frames) {
        if (frame.acquired != VK_NULL_HANDLE)
            vkDestroySemaphore(vk.device, frame.acquired, nullptr);
    }
    if (timeline != VK_NULL_HANDLE)
        vkDestroySemaphore(vk.device, timeline, nullptr);
    if (pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(vk.device, pipeline, nullptr);
    if (pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
    for (auto &frame : frames)
        frame.uniforms = {};
    if (descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(vk.device, descriptor_pool, nullptr);
    if (descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(vk.device, descriptor_layout, nullptr);
    if (command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(vk.device, command_pool, nullptr);
}

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

std::expected<std::unique_ptr<Renderer>, Error>
Renderer::create(const Window &window, const VulkanContext &context,
                 std::span<const SceneMesh> meshes) {
    auto renderer = std::unique_ptr<Renderer>(new Renderer);
    renderer->m_impl = std::make_unique<Impl>();
    renderer->m_impl->vk = context.device();
    renderer->m_impl->window = &window;
    if (auto result = renderer->m_impl->initialize(meshes); !result)
        return std::unexpected(result.error());
    return renderer;
}

void Renderer::process_event(const SDL_Event &event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}
bool Renderer::ui_wants_mouse() const {
    return m_impl->imgui_context && ImGui::GetIO().WantCaptureMouse;
}
std::expected<bool, Error> Renderer::draw(const FrameView &view) {
    return m_impl->draw(view);
}

} // namespace mj
