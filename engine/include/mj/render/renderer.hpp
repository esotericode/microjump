#pragma once

#include <mj/core/error.hpp>
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
};

// A soft contact shadow under the player. Placeholder depth cue until the M3 shadow maps.
struct BlobShadow {
    Vec3 caster;      // Feet position of the caster.
    float radius = 0; // Zero disables it.
};

struct FrameView {
    CameraPose camera;
    std::span<const DrawInstance> instances;
    BlobShadow blob;
    double elapsed = 0; // Real seconds since the previous frame, for the FPS readout.
    bool overlay = true;
    // Extra lines inside the diagnostics overlay, then free-standing debug windows (ImGui).
    const std::function<void()> *overlay_lines = nullptr;
    const std::function<void()> *debug_windows = nullptr;
    const std::filesystem::path *capture = nullptr;
};

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
    // false means minimized/out-of-date: no rendered frame is counted.
    [[nodiscard]] std::expected<bool, Error> draw(const FrameView &view);

  private:
    struct Impl;
    Renderer();
    std::unique_ptr<Impl> m_impl;
};
} // namespace mj
