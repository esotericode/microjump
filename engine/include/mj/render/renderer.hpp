#pragma once

#include <mj/core/error.hpp>
#include <mj/scene/debug_camera.hpp>
#include <mj/scene/mesh.hpp>

#include <expected>
#include <filesystem>
#include <memory>

union SDL_Event;

namespace mj {
class Window;
class VulkanContext;

class Renderer {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<Renderer>, Error>
    create(const Window &window, const VulkanContext &context, const SceneMesh &mesh);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void process_event(const SDL_Event &event);
    // false means minimized/out-of-date: no rendered frame is counted.
    [[nodiscard]] std::expected<bool, Error> draw(const CameraPose &camera, double elapsed,
                                                  bool overlay,
                                                  const std::filesystem::path *capture = nullptr);

  private:
    struct Impl;
    Renderer();
    std::unique_ptr<Impl> m_impl;
};
} // namespace mj
