#pragma once

#include <mj/core/error.hpp>
#include <mj/render/lighting.hpp>
#include <mj/render/pacing.hpp>
#include <mj/scene/debug_camera.hpp>
#include <mj/scene/mesh.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

union SDL_Event;

namespace mj {
class Window;
class VulkanContext;

struct MeshHandle {
    std::uint32_t index = 0;
};

struct DrawInstance {
    MeshHandle mesh;
    Mat4 model;                 // Rigid transform (rotation + translation only).
    float grid = 0;             // Strength of the 1 m world-space grid lines (0..1).
    bool receives_blob = false; // Darkened by the blob shadow below the caster.
    bool casts_shadow = true;
    Vec3 tint{1, 1, 1}; // Multiplies the vertex albedo.
    float emissive = 0; // Adds albedo * tint * emissive, unlit (light bulbs).
};

// A soft contact shadow directly under the player: a platforming cue that stays useful when
// the sun's shadow falls sideways or the player is in shade.
struct BlobShadow {
    Vec3 caster;      // Feet position of the caster.
    float radius = 0; // Zero disables it.
    float strength = 0.35F;
};

struct FrameView {
    CameraPose camera;
    std::span<const DrawInstance> instances;
    BlobShadow blob;
    Environment environment;
    std::span<const LocalLight> lights; // At most k_max_local_lights are used.
    ShadowSettings shadows;
    double elapsed = 0; // Real seconds since the previous frame, for the FPS readout.
    bool overlay = true;
    // Extra lines inside the diagnostics overlay, then free-standing debug windows (ImGui).
    const std::function<void()> *overlay_lines = nullptr;
    const std::function<void()> *debug_windows = nullptr;
    const std::filesystem::path *capture = nullptr;
};

// Where the CPU spent the last draw call, for frame-pacing diagnostics. Times are
// milliseconds; gpu_ms is the GPU time of this slot's previous frame (-1 if unavailable).
struct FrameTiming {
    double display_wait_ms = 0; // wait_for_display before this frame's input and time.
    double rebuild_ms = 0;      // Device idle wait plus shadow/swapchain rebuild, if any.
    double wait_ms = 0;         // Waiting for the frame slot's previous GPU work.
    double acquire_ms = 0;      // vkAcquireNextImageKHR.
    double record_ms = 0;       // Overlay, recording and submission.
    double present_ms = 0;      // vkQueuePresentKHR.
    double gpu_ms = -1;
    std::uint32_t image = 0;
    std::uint32_t swapchain_images = 0;
    bool shadow_rebuilt = false;
    bool swapchain_rebuilt = false;
    bool suboptimal = false;      // Acquire or present asked for a swapchain rebuild.
    bool display_timeout = false; // The display wait gave up (e.g. a hidden window).
};

// One-shot diagnostic actions, applied at the start of the next draw.
enum class RenderAction : std::uint8_t { wait_idle, rebuild_shadow_map, rebuild_swapchain };

class Renderer {
  public:
    // Meshes are uploaded once; handle i refers to meshes[i].
    [[nodiscard]] static std::expected<std::unique_ptr<Renderer>, Error>
    create(const Window &window, const VulkanContext &context, std::span<const SceneMesh> meshes);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void process_event(const SDL_Event &event);
    // ImGui wants the mouse (an interactive debug window is hovered or active).
    [[nodiscard]] bool ui_wants_mouse() const;
    // ImGui is taking typed text (e.g. Ctrl+click numeric entry); gameplay keys must wait.
    [[nodiscard]] bool ui_wants_keyboard() const;
    // false means minimized/out-of-date: no rendered frame is counted.
    [[nodiscard]] std::expected<bool, Error> draw(const FrameView &view);
    [[nodiscard]] const FrameTiming &last_timing() const;
    void request(RenderAction action);
    // Blocks until the display has shown the earlier frame `pacing` allows to be queued.
    // Call it before sampling input and time for the next frame. Does nothing when the
    // device lacks VK_KHR_present_wait or pacing is disabled.
    [[nodiscard]] std::expected<void, Error> wait_for_display(const DisplayPacing &pacing);
    [[nodiscard]] bool display_pacing_supported() const;

  private:
    struct Impl;
    Renderer();
    std::unique_ptr<Impl> m_impl;
};
} // namespace mj
