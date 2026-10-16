#include <mj/core/png.hpp>
#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include "resources.hpp"

#include <SDL3/SDL_video.h>
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
constexpr VkFormat k_color_format = VK_FORMAT_R8G8B8A8_SRGB; // Tone-mapped output and captures.
constexpr VkFormat k_hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat k_depth_format = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat k_shadow_format = VK_FORMAT_D16_UNORM;
constexpr std::size_t k_frame_count = 2;

using Float4 = std::array<float, 4>;
Float4 float4(Vec3 v, float w = 0) {
    return {v.x, v.y, v.z, w};
}

// std140 mirrors of the shaders' Frame block and Draw push constants.
struct GpuLight {
    Float4 position_range;
    Float4 color;
    Float4 direction_cos_outer;
    Float4 cos_inner;
};
struct FrameUniforms {
    Mat4 view_projection;
    Mat4 inverse_view_projection;
    Mat4 light_view_projection;
    Float4 camera_position;
    Float4 sun_direction;
    Float4 sun_color;
    Float4 sky_zenith;
    Float4 sky_horizon; // w: fog density.
    Float4 ground;
    Float4 blob_shadow; // xyz caster feet, w radius.
    Float4 shadow; // x texel meters, y normal offset (texels), z kernel radius, w 1/resolution.
    Float4 counts; // x local lights, y blob strength.
    std::array<GpuLight, k_max_local_lights> lights;
};
struct DrawConstants {
    std::array<Float4, 3> model_rows;
    Float4 material; // x grid strength, y receives blob shadow, z emissive.
    Float4 tint;
};
static_assert(sizeof(DrawConstants) == 80);
struct PostConstants {
    Float4 exposure; // x exposure.
};

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

enum class VertexInput : std::uint8_t { none, position, full };

struct PipelineDescription {
    const char *vertex_shader;
    const char *fragment_shader; // nullptr: depth only.
    VkPipelineLayout layout;
    VertexInput vertex_input;
    VkCullModeFlags cull;
    bool depth_test;
    bool depth_write;
    VkCompareOp depth_compare;
    bool depth_bias;
    VkFormat color_format; // VK_FORMAT_UNDEFINED: no color attachment.
    VkFormat depth_format; // VK_FORMAT_UNDEFINED: no depth attachment.
};

std::expected<VkPipeline, Error> create_pipeline(VkDevice device,
                                                 const PipelineDescription &description) {
    std::array<VkShaderModule, 2> modules{};
    auto release = [&] {
        for (const auto module : modules) {
            if (module != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, module, nullptr);
        }
    };
    auto vertex = load_shader(device, description.vertex_shader);
    if (!vertex)
        return std::unexpected(vertex.error());
    modules[0] = *vertex;
    std::uint32_t stage_count = 1;
    if (description.fragment_shader != nullptr) {
        auto fragment = load_shader(device, description.fragment_shader);
        if (!fragment) {
            release();
            return std::unexpected(fragment.error());
        }
        modules[1] = *fragment;
        stage_count = 2;
    }
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (std::size_t i = 0; i < stages.size(); ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].pName = "main";
        stages[i].module = modules[i];
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription, 4> attributes{
        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
         {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
         {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
         {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, surface)}}};
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (description.vertex_input != VertexInput::none) {
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = description.vertex_input == VertexInput::full
                                                    ? static_cast<std::uint32_t>(attributes.size())
                                                    : 1;
        input.pVertexAttributeDescriptions = attributes.data();
    }
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
    raster.cullMode = description.cull;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = description.depth_bias ? VK_TRUE : VK_FALSE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = description.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = description.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = description.depth_compare;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    const bool has_color = description.color_format != VK_FORMAT_UNDEFINED;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = has_color ? 1 : 0;
    blend.pAttachments = &attachment;
    std::vector<VkDynamicState> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (description.depth_bias)
        dynamic_states.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size());
    dynamic.pDynamicStates = dynamic_states.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = has_color ? 1 : 0;
    rendering.pColorAttachmentFormats = &description.color_format;
    rendering.depthAttachmentFormat = description.depth_format;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = stage_count;
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = description.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto result =
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    release();
    if (result != VK_SUCCESS)
        return std::unexpected(vulkan_error("vkCreateGraphicsPipelines", result));
    return pipeline;
}

void set_viewport(VkCommandBuffer command, VkExtent2D extent) {
    const VkViewport viewport{
        0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
}

bool supports(VkPhysicalDevice physical, VkFormat format, VkFormatFeatureFlags features) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
    return (properties.optimalTilingFeatures & features) == features;
}
} // namespace

struct Renderer::Impl {
    struct Frame {
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::uint64_t completion = 0;
        Image hdr;
        Image color;
        Image depth;
        Buffer readback;
        Buffer uniforms;
        VkDescriptorSet frame_set = VK_NULL_HANDLE; // Uniforms + shadow map.
        VkDescriptorSet post_set = VK_NULL_HANDLE;  // HDR image for tone mapping.
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
    Image shadow_map;
    ShadowQuality shadow_quality = ShadowQuality::medium;
    std::uint32_t shadow_resolution = 0;
    VkSampler shadow_sampler = VK_NULL_HANDLE;
    VkSampler post_sampler = VK_NULL_HANDLE;
    bool shadow_linear = true;
    VkDescriptorSetLayout frame_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout post_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout scene_layout = VK_NULL_HANDLE;
    VkPipelineLayout post_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline scene_pipeline = VK_NULL_HANDLE;
    VkPipeline sky_pipeline = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline = VK_NULL_HANDLE;
    VkPipeline tonemap_pipeline = VK_NULL_HANDLE;
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
    bool imgui_context = false;
    bool imgui_platform = false;
    bool imgui_renderer = false;
    bool recreate = false;
    double smoothed_seconds = 1.0 / 60;

    ~Impl();
    std::expected<void, Error> initialize(std::span<const SceneMesh> scene_meshes);
    std::expected<void, Error> create_descriptors();
    std::expected<void, Error> create_pipelines();
    std::expected<void, Error> create_shadow_map(ShadowQuality quality);
    std::expected<void, Error> create_swapchain(PixelSize size);
    std::expected<void, Error> initialize_overlay();
    std::expected<void, Error> wait(std::uint64_t value) const;
    void destroy_swapchain();
    void overlay(const FrameView &view);
    void draw_instances(VkCommandBuffer command, const FrameView &view, bool shadow_pass) const;
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
    if (!supports(vk.physical, k_color_format,
                  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !supports(vk.physical, k_hdr_format,
                  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        !supports(vk.physical, k_depth_format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
        !supports(vk.physical, k_shadow_format,
                  VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        return std::unexpected(
            Error{"GPU lacks required sRGB / RGBA16F / D32 / D16 shadow image formats"});
    }
    // Hardware bilinear shadow comparison smooths each tap; fall back to nearest taps.
    shadow_linear =
        supports(vk.physical, k_shadow_format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
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
    if (auto result = create_pipelines(); !result)
        return result;
    const auto size = window->pixel_size();
    if (size.width == 0 || size.height == 0)
        return std::unexpected(Error{"Initial window has no drawable area"});
    if (auto result = create_swapchain(size); !result)
        return result;
    return initialize_overlay();
}

std::expected<void, Error> Renderer::Impl::create_descriptors() {
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = shadow_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.minFilter = sampler.magFilter;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // Outside the map: lit.
    sampler.compareEnable = VK_TRUE;
    sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sampler.maxLod = 0;
    MJ_VK_TRY(vkCreateSampler(vk.device, &sampler, nullptr, &shadow_sampler));
    sampler = {};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    MJ_VK_TRY(vkCreateSampler(vk.device, &sampler, nullptr, &post_sampler));

    const std::array<VkDescriptorSetLayoutBinding, 2> frame_bindings{
        {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}}};
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<std::uint32_t>(frame_bindings.size());
    layout_info.pBindings = frame_bindings.data();
    MJ_VK_TRY(vkCreateDescriptorSetLayout(vk.device, &layout_info, nullptr, &frame_layout));
    const VkDescriptorSetLayoutBinding post_binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &post_binding;
    MJ_VK_TRY(vkCreateDescriptorSetLayout(vk.device, &layout_info, nullptr, &post_layout));

    const std::array<VkDescriptorPoolSize, 2> pool_sizes{
        {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_frame_count},
         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_frame_count * 2}}};
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = k_frame_count * 2;
    pool_info.poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    MJ_VK_TRY(vkCreateDescriptorPool(vk.device, &pool_info, nullptr, &descriptor_pool));
    for (auto &frame : frames) {
        auto uniforms = Buffer::create(
            vk.device, vk.physical,
            {.size = sizeof(FrameUniforms), .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT});
        if (!uniforms)
            return std::unexpected(uniforms.error());
        frame.uniforms = std::move(*uniforms);
        const std::array layouts{frame_layout, post_layout};
        std::array<VkDescriptorSet, 2> sets{};
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = descriptor_pool;
        allocation.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
        allocation.pSetLayouts = layouts.data();
        MJ_VK_TRY(vkAllocateDescriptorSets(vk.device, &allocation, sets.data()));
        frame.frame_set = sets[0];
        frame.post_set = sets[1];
        VkDescriptorBufferInfo buffer_info{frame.uniforms.handle, 0, sizeof(FrameUniforms)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.frame_set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
    }

    const VkPushConstantRange scene_push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, sizeof(DrawConstants)};
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &frame_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &scene_push;
    MJ_VK_TRY(vkCreatePipelineLayout(vk.device, &pipeline_layout_info, nullptr, &scene_layout));
    const VkPushConstantRange post_push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostConstants)};
    pipeline_layout_info.pSetLayouts = &post_layout;
    pipeline_layout_info.pPushConstantRanges = &post_push;
    MJ_VK_TRY(
        vkCreatePipelineLayout(vk.device, &pipeline_layout_info, nullptr, &post_pipeline_layout));
    return {};
}

std::expected<void, Error> Renderer::Impl::create_pipelines() {
    const std::array<std::pair<VkPipeline *, PipelineDescription>, 4> pipelines{{
        {&shadow_pipeline,
         {.vertex_shader = "shadow.vert.spv",
          .fragment_shader = nullptr,
          .layout = scene_layout,
          .vertex_input = VertexInput::position,
          .cull = VK_CULL_MODE_NONE, // Thin pads and walls must still cast.
          .depth_test = true,
          .depth_write = true,
          .depth_compare = VK_COMPARE_OP_LESS,
          .depth_bias = true,
          .color_format = VK_FORMAT_UNDEFINED,
          .depth_format = k_shadow_format}},
        {&sky_pipeline,
         {.vertex_shader = "fullscreen.vert.spv",
          .fragment_shader = "sky.frag.spv",
          .layout = scene_layout,
          .vertex_input = VertexInput::none,
          .cull = VK_CULL_MODE_NONE,
          .depth_test = false,
          .depth_write = false,
          .depth_compare = VK_COMPARE_OP_ALWAYS,
          .depth_bias = false,
          .color_format = k_hdr_format,
          .depth_format = k_depth_format}},
        {&scene_pipeline,
         {.vertex_shader = "scene.vert.spv",
          .fragment_shader = "scene.frag.spv",
          .layout = scene_layout,
          .vertex_input = VertexInput::full,
          .cull = VK_CULL_MODE_BACK_BIT, // Generated primitives are closed and CCW.
          .depth_test = true,
          .depth_write = true,
          .depth_compare = VK_COMPARE_OP_GREATER,
          .depth_bias = false,
          .color_format = k_hdr_format,
          .depth_format = k_depth_format}},
        {&tonemap_pipeline,
         {.vertex_shader = "fullscreen.vert.spv",
          .fragment_shader = "tonemap.frag.spv",
          .layout = post_pipeline_layout,
          .vertex_input = VertexInput::none,
          .cull = VK_CULL_MODE_NONE,
          .depth_test = false,
          .depth_write = false,
          .depth_compare = VK_COMPARE_OP_ALWAYS,
          .depth_bias = false,
          .color_format = k_color_format,
          .depth_format = VK_FORMAT_UNDEFINED}},
    }};
    for (const auto &[target, description] : pipelines) {
        auto pipeline = create_pipeline(vk.device, description);
        if (!pipeline)
            return std::unexpected(pipeline.error());
        *target = *pipeline;
    }
    return {};
}

// Callers wait for the device to be idle: both frame descriptor sets reference the map.
std::expected<void, Error> Renderer::Impl::create_shadow_map(ShadowQuality quality) {
    const auto resolution = shadow_preset(quality).resolution;
    auto image = Image::create(
        vk.device, vk.physical,
        {.extent = {resolution, resolution},
         .format = k_shadow_format,
         .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
         .aspect = VK_IMAGE_ASPECT_DEPTH_BIT});
    if (!image)
        return std::unexpected(image.error());
    shadow_map = std::move(*image);
    shadow_quality = quality;
    shadow_resolution = resolution;
    for (const auto &frame : frames) {
        VkDescriptorImageInfo image_info{shadow_sampler, shadow_map.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.frame_set;
        write.dstBinding = 1;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
    }
    log("Shadow map: " + std::string(to_string(quality)) + " " + std::to_string(resolution) + "x" +
        std::to_string(resolution) + (shadow_linear ? ", bilinear PCF" : ", nearest PCF"));
    return {};
}

void Renderer::Impl::destroy_swapchain() {
    for (auto &frame : frames) {
        frame.hdr = {};
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
    if (!supports(vk.physical, swap_format, VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
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
        const std::array<std::pair<Image *, ImageDescription>, 3> targets{
            {{&frame.hdr,
              {.extent = extent,
               .format = k_hdr_format,
               .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               .aspect = VK_IMAGE_ASPECT_COLOR_BIT}},
             {&frame.color,
              {.extent = extent,
               .format = k_color_format,
               .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
               .aspect = VK_IMAGE_ASPECT_COLOR_BIT}},
             {&frame.depth,
              {.extent = extent,
               .format = k_depth_format,
               .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
               .aspect = VK_IMAGE_ASPECT_DEPTH_BIT}}}};
        for (const auto &[target, description] : targets) {
            auto image = Image::create(vk.device, vk.physical, description);
            if (!image)
                return std::unexpected(image.error());
            *target = std::move(*image);
        }
        VkDescriptorImageInfo image_info{post_sampler, frame.hdr.view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.post_set;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
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
        // Gamepad-only navigation: keyboard players keep WASD/Space for movement while
        // adjusting the tuning window with the mouse.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
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
    info.PipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
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
    const int taps = shadow_preset(shadow_quality).kernel_radius * 2 + 1;
    ImGui::Text("Shadows: %s %u  PCF %dx%d   |   Lights: %zu",
                std::string(to_string(shadow_quality)).c_str(), shadow_resolution, taps, taps,
                std::min(view.lights.size(), k_max_local_lights));
    if (view.overlay_lines != nullptr && *view.overlay_lines) {
        ImGui::Separator();
        (*view.overlay_lines)();
    }
    ImGui::End();
}

void Renderer::Impl::draw_instances(VkCommandBuffer command, const FrameView &view,
                                    bool shadow_pass) const {
    for (const auto &instance : view.instances) {
        if (instance.mesh.index >= meshes.size() || (shadow_pass && !instance.casts_shadow))
            continue;
        DrawConstants constants{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 4; ++column)
                constants.model_rows[row][column] = instance.model.at(row, column);
        }
        constants.material = {instance.grid, instance.receives_blob ? 1.0F : 0.0F,
                              instance.emissive, 0};
        constants.tint = float4(instance.tint);
        vkCmdPushConstants(command, scene_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        const auto &range = meshes[instance.mesh.index];
        vkCmdDraw(command, range.count, 1, range.first, 0);
    }
}

std::expected<bool, Error> Renderer::Impl::draw(const FrameView &view) {
    const auto *capture = view.capture;
    if (std::isfinite(view.elapsed) && view.elapsed > 0)
        smoothed_seconds += (view.elapsed - smoothed_seconds) * 0.08;
    const auto size = window->pixel_size();
    if (size.width == 0 || size.height == 0)
        return false;
    const bool resized =
        recreate || size.width != requested_size.width || size.height != requested_size.height;
    // The shadow map is created on first use, at the requested quality.
    const bool shadow_changed =
        shadow_map.handle == VK_NULL_HANDLE || view.shadows.quality != shadow_quality;
    if (resized || shadow_changed) {
        MJ_VK_TRY(vkDeviceWaitIdle(vk.device));
        if (shadow_changed) {
            if (auto result = create_shadow_map(view.shadows.quality); !result)
                return std::unexpected(result.error());
        }
        if (resized) {
            if (imgui_renderer) {
                ImGui_ImplVulkan_Shutdown();
                imgui_renderer = false;
            }
            if (auto result = create_swapchain(size); !result)
                return std::unexpected(result.error());
            if (auto result = initialize_overlay(); !result)
                return std::unexpected(result.error());
        }
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
    auto &io = ImGui::GetIO();
    if ((SDL_GetWindowFlags(window->native_handle()) & SDL_WINDOW_INPUT_FOCUS) != 0)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (view.overlay)
        overlay(view);
    if (view.debug_windows != nullptr && *view.debug_windows)
        (*view.debug_windows)();
    ImGui::Render();

    const auto &camera = view.camera;
    const auto &environment = view.environment;
    const auto shadow = fit_shadow(environment.sun_direction, view.shadows);
    FrameUniforms uniforms{};
    uniforms.view_projection = perspective_reverse_z({.vertical_fov = 1.04719755F,
                                                      .aspect = static_cast<float>(extent.width) /
                                                                static_cast<float>(extent.height),
                                                      .near_plane = 0.1F}) *
                               view_matrix(camera.position, camera.yaw, camera.pitch);
    uniforms.inverse_view_projection = inverse(uniforms.view_projection);
    uniforms.light_view_projection = shadow.view_projection;
    uniforms.camera_position = float4(camera.position, 1);
    uniforms.sun_direction = float4(normalized(environment.sun_direction));
    uniforms.sun_color = float4(environment.sun_color);
    uniforms.sky_zenith = float4(environment.sky_zenith);
    uniforms.sky_horizon = float4(environment.sky_horizon, environment.fog_density);
    uniforms.ground = float4(environment.ground);
    uniforms.blob_shadow = float4(view.blob.caster, view.blob.radius);
    uniforms.shadow = {shadow.texel_size, view.shadows.normal_offset,
                       static_cast<float>(shadow_preset(shadow_quality).kernel_radius),
                       1.0F / static_cast<float>(shadow_resolution)};
    const std::size_t light_count = std::min(view.lights.size(), k_max_local_lights);
    uniforms.counts = {static_cast<float>(light_count), view.blob.strength, 0, 0};
    for (std::size_t i = 0; i < light_count; ++i) {
        const auto &light = view.lights[i];
        uniforms.lights[i] = {float4(light.position, light.range),
                              float4(light.color),
                              float4(normalized(light.direction), std::cos(light.outer_cone)),
                              {std::cos(light.inner_cone), 0, 0, 0}};
    }
    std::memcpy(frame.uniforms.mapped, &uniforms, sizeof(uniforms));
    if (auto flushed = frame.uniforms.cache(false); !flushed)
        return std::unexpected(flushed.error());

    const auto command = frame.command;
    MJ_VK_TRY(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    MJ_VK_TRY(vkBeginCommandBuffer(command, &begin));
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertices.handle, &offset);

    // Sun shadow map. The single map is shared by both frame slots: wait for earlier frames'
    // fragment reads (and the previous write) before overwriting it.
    image_barrier(command, shadow_map.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .new_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   .source_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   .destination_stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   .destination_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT});
    VkRenderingAttachmentInfo shadow_depth{};
    shadow_depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    shadow_depth.imageView = shadow_map.view;
    shadow_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    shadow_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    shadow_depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    shadow_depth.clearValue.depthStencil = {1, 0};
    const VkExtent2D shadow_extent{shadow_resolution, shadow_resolution};
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, shadow_extent};
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &shadow_depth;
    vkCmdBeginRendering(command, &rendering);
    set_viewport(command, shadow_extent);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout, 0, 1,
                            &frame.frame_set, 0, nullptr);
    vkCmdSetDepthBias(command, view.shadows.constant_bias, 0, view.shadows.slope_bias);
    draw_instances(command, view, true);
    vkCmdEndRendering(command);
    image_barrier(command, shadow_map.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                   .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   .source_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   .destination_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   .destination_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT});

    // HDR scene: sky, then lit geometry.
    image_barrier(command, frame.hdr.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
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
    VkRenderingAttachmentInfo hdr{};
    hdr.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    hdr.imageView = frame.hdr.view;
    hdr.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    hdr.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // The sky covers every pixel.
    hdr.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = frame.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {0, 0};
    rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &hdr;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(command, &rendering);
    set_viewport(command, extent);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, sky_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout, 0, 1,
                            &frame.frame_set, 0, nullptr);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_pipeline);
    draw_instances(command, view, false);
    vkCmdEndRendering(command);

    // Tone map into the sRGB target, then the overlay.
    image_barrier(command, frame.hdr.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   .source_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   .destination_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                   .destination_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT});
    image_barrier(command, frame.color.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                  {.old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   .source_stage = VK_PIPELINE_STAGE_2_NONE,
                   .source_access = 0,
                   .destination_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   .destination_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = frame.color.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // The tone-map triangle covers it.
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = nullptr;
    vkCmdBeginRendering(command, &rendering);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline_layout, 0, 1,
                            &frame.post_set, 0, nullptr);
    const PostConstants post{{environment.exposure, 0, 0, 0}};
    vkCmdPushConstants(command, post_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(post),
                       &post);
    vkCmdDraw(command, 3, 1, 0, 0);
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
    shadow_map = {};
    for (auto &frame : frames) {
        frame.uniforms = {};
        if (frame.acquired != VK_NULL_HANDLE)
            vkDestroySemaphore(vk.device, frame.acquired, nullptr);
    }
    if (timeline != VK_NULL_HANDLE)
        vkDestroySemaphore(vk.device, timeline, nullptr);
    for (const auto pipeline : {shadow_pipeline, sky_pipeline, scene_pipeline, tonemap_pipeline}) {
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(vk.device, pipeline, nullptr);
    }
    for (const auto layout : {scene_layout, post_pipeline_layout}) {
        if (layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(vk.device, layout, nullptr);
    }
    if (descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(vk.device, descriptor_pool, nullptr);
    for (const auto layout : {frame_layout, post_layout}) {
        if (layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(vk.device, layout, nullptr);
    }
    for (const auto sampler : {shadow_sampler, post_sampler}) {
        if (sampler != VK_NULL_HANDLE)
            vkDestroySampler(vk.device, sampler, nullptr);
    }
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
bool Renderer::ui_wants_keyboard() const {
    return m_impl->imgui_context && ImGui::GetIO().WantCaptureKeyboard;
}
std::expected<bool, Error> Renderer::draw(const FrameView &view) {
    return m_impl->draw(view);
}

} // namespace mj
