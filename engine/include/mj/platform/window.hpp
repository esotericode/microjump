#pragma once

#include <mj/core/error.hpp>

#include <expected>
#include <memory>

struct SDL_Window;

namespace mj {

// Owns SDL video initialization as well as the window. One window in M0.
class Window {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<Window>, Error> create();
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    [[nodiscard]] SDL_Window *native_handle() const;
    [[nodiscard]] bool poll_quit();
    void wait_for_events() const;

  private:
    Window() = default;
    SDL_Window *m_window = nullptr;
    bool m_sdl_initialized = false;
};

} // namespace mj
