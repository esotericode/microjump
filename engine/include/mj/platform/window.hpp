#pragma once

#include <mj/core/error.hpp>
#include <mj/scene/debug_camera.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>

struct SDL_Window;
struct SDL_Gamepad;
union SDL_Event;

namespace mj {

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Device state for one rendered frame, merged from keyboard/mouse and the first gamepad.
struct InputState {
    float move_x = 0; // Left stick or WASD: +X right, +Y forward, length <= 1.
    float move_y = 0;
    float look_x = 0; // Relative mouse counts while the mouse is captured.
    float look_y = 0;
    float look_stick_x = 0; // Right stick after the dead zone, -1..1.
    float look_stick_y = 0;
    bool jump_held = false;
    bool jump_pressed = false; // A press happened during this poll (survives sub-frame taps).
    bool walk_held = false;
    bool long_jump_held = false;
    CameraInput camera; // Free-flight debug camera.
    bool quit = false;
    bool toggle_overlay = false;
    bool toggle_tuning = false;
    bool toggle_free_camera = false;
    bool reset_pressed = false;
    bool gamepad_connected = false;
};

// Owns SDL video initialization and the window. Vulkan resources must die first.
class Window {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<Window>, Error> create();
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    [[nodiscard]] SDL_Window *native_handle() const;
    // Captures the mouse for looking while the window is focused and `capture_mouse` is set.
    [[nodiscard]] InputState poll_events(const std::function<void(const SDL_Event &)> &handler,
                                         bool capture_mouse);
    [[nodiscard]] PixelSize pixel_size() const;
    [[nodiscard]] std::expected<void, Error> resize(int width, int height);
    [[nodiscard]] static std::filesystem::path executable_directory();
    void wait_for_events() const;

  private:
    Window() = default;
    SDL_Window *m_window = nullptr;
    SDL_Gamepad *m_gamepad = nullptr;
    bool m_sdl_initialized = false;
    bool m_mouse_captured = false;
    std::uint64_t m_capture_started_ns = 0;
};

// Radial dead zone with rescaling, so small stick drift is ignored and full tilt reaches 1.
struct StickReading {
    float x = 0;
    float y = 0;
};
[[nodiscard]] StickReading apply_dead_zone(StickReading raw, float dead_zone);

} // namespace mj
