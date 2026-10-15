#pragma once

#include <mj/core/error.hpp>
#include <mj/scene/debug_camera.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>

struct SDL_Window;
union SDL_Event;

namespace mj {

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
struct InputState {
    CameraInput camera;
    float look_x = 0;
    float look_y = 0;
    bool quit = false;
    bool toggle_overlay = false;
    bool reset_camera = false;
};

// Owns SDL video initialization and the window. Vulkan resources must die first.
class Window {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<Window>, Error> create();
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    [[nodiscard]] SDL_Window *native_handle() const;
    [[nodiscard]] InputState poll_events(const std::function<void(const SDL_Event &)> &handler);
    [[nodiscard]] PixelSize pixel_size() const;
    [[nodiscard]] std::expected<void, Error> resize(int width, int height);
    [[nodiscard]] static std::filesystem::path executable_directory();
    void wait_for_events() const;

  private:
    Window() = default;
    SDL_Window *m_window = nullptr;
    bool m_sdl_initialized = false;
    bool m_mouse_captured = false;
};

} // namespace mj
