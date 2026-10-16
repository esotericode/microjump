#include <mj/platform/window.hpp>

#include <SDL3/SDL.h>

#include <cmath>

namespace mj {
std::expected<std::unique_ptr<Window>, Error> Window::create() {
    auto window = std::unique_ptr<Window>(new Window);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        return std::unexpected(Error{"SDL_Init: " + std::string(SDL_GetError())});
    }
    window->m_sdl_initialized = true;
    window->m_window =
        SDL_CreateWindow("microjump", 1280, 720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window->m_window == nullptr) {
        return std::unexpected(Error{"SDL_CreateWindow: " + std::string(SDL_GetError())});
    }
    window->m_gamepad.open_available();
    return window;
}

Window::~Window() {
    m_gamepad.close();
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
    if (m_sdl_initialized) {
        SDL_Quit();
    }
}

SDL_Window *Window::native_handle() const {
    return m_window;
}

InputState Window::poll_events(const std::function<void(const SDL_Event &)> &handler,
                               PollOptions options) {
    SDL_Event event;
    InputState input;
    while (SDL_PollEvent(&event)) {
        handler(event);
        input.quit = input.quit || event.type == SDL_EVENT_QUIT ||
                     event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                     (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            const auto key = event.key.key;
            input.toggle_overlay = input.toggle_overlay || key == SDLK_F1;
            input.toggle_tuning = input.toggle_tuning || key == SDLK_F2;
            input.toggle_free_camera = input.toggle_free_camera || key == SDLK_F3;
            input.reset_pressed = input.reset_pressed || key == SDLK_R || key == SDLK_HOME;
            input.jump_pressed = input.jump_pressed || key == SDLK_SPACE;
            if (key == SDLK_SPACE && (event.key.mod & SDL_KMOD_SHIFT) != 0)
                input.long_jump_held = true; // Keep a complete Shift+Space tap in one poll.
            input.dive_pressed = input.dive_pressed || key == SDLK_E;
        }
        // A click that captures the mouse is not a dive; only clicks while playing count.
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
            m_mouse_captured)
            input.dive_pressed = true;
        m_gamepad.process_event(event);
    }
    const auto flags = SDL_GetWindowFlags(m_window);
    const bool focused =
        (flags & SDL_WINDOW_INPUT_FOCUS) != 0 && (flags & SDL_WINDOW_MINIMIZED) == 0;
    const bool capture = focused && options.capture_mouse;
    const bool changed = capture != m_mouse_captured;
    if (changed) {
        if (!SDL_SetWindowRelativeMouseMode(m_window, capture)) {
            log(SDL_GetError());
        } else {
            m_mouse_captured = capture;
            m_capture_started_ns = SDL_GetTicksNS();
        }
    }
    float dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);
    // Engaging the pointer lock can report a one-off jump (seen ~10 frames later on Wayland),
    // sized by wherever the cursor was. Ignore motion briefly so the camera never twitches.
    constexpr std::uint64_t k_capture_settle_ns = 150'000'000;
    const bool settled = SDL_GetTicksNS() - m_capture_started_ns >= k_capture_settle_ns;
    if (capture && !changed && settled) {
        input.look_x = dx;
        input.look_y = dy;
    }
    const auto pad = m_gamepad.sample(gamepad_tuning, focused);
    input.focused = focused;
    input.mouse_captured = m_mouse_captured;
    input.gamepad_connected = pad.connected;
    input.gamepad_name = pad.name;
    input.gamepad_labels = pad.labels;
    if (!focused) {
        // Ignore gameplay and UI press edges received while the window is inactive.
        const bool quit = input.quit;
        input = {};
        input.quit = quit;
        input.mouse_captured = m_mouse_captured;
        input.gamepad_connected = pad.connected;
        input.gamepad_name = pad.name;
        input.gamepad_labels = pad.labels;
        return input;
    }
    const bool *keys = SDL_GetKeyboardState(nullptr);
    auto key = [keys](SDL_Scancode code) { return keys[code] ? 1.0F : 0.0F; };
    const float right = key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A);
    const float forward = key(SDL_SCANCODE_W) - key(SDL_SCANCODE_S);
    input.camera.strafe = right;
    input.camera.forward = forward;
    input.camera.vertical = key(SDL_SCANCODE_E) - key(SDL_SCANCODE_Q);
    input.camera.fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    input.jump_held = keys[SDL_SCANCODE_SPACE];
    input.long_jump_held = input.long_jump_held || input.camera.fast;
    input.walk_held = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    const float keyboard_length = std::sqrt(right * right + forward * forward);
    if (keyboard_length > 0) {
        input.move_x = right / keyboard_length;
        input.move_y = forward / keyboard_length;
    }
    input.toggle_tuning = input.toggle_tuning || pad.toggle_tuning;
    input.toggle_overlay = input.toggle_overlay || pad.toggle_overlay;
    if (!options.gamepad_gameplay)
        return input;
    if (pad.move.x * pad.move.x + pad.move.y * pad.move.y >
        input.move_x * input.move_x + input.move_y * input.move_y) {
        input.move_x = pad.move.x;
        input.move_y = pad.move.y;
    }
    input.look_stick_x = pad.look.x;
    input.look_stick_y = pad.look.y;
    input.jump_held = input.jump_held || pad.jump_held;
    input.jump_pressed = input.jump_pressed || pad.jump_pressed;
    input.long_jump_held = input.long_jump_held || pad.long_jump_held;
    input.dive_pressed = input.dive_pressed || pad.dive_pressed;
    input.walk_held = input.walk_held || pad.walk_held;
    input.reset_pressed = input.reset_pressed || pad.reset_pressed;
    input.recenter_pressed = pad.recenter_pressed;
    return input;
}

PixelSize Window::pixel_size() const {
    int width = 0, height = 0;
    if ((SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0)
        return {};
    if (!SDL_GetWindowSizeInPixels(m_window, &width, &height))
        return {};
    return {static_cast<std::uint32_t>(std::max(width, 0)),
            static_cast<std::uint32_t>(std::max(height, 0))};
}

std::expected<void, Error> Window::resize(int width, int height) {
    if (!SDL_SetWindowSize(m_window, width, height) || !SDL_SyncWindow(m_window)) {
        return std::unexpected(Error{"Window resize: " + std::string(SDL_GetError())});
    }
    return {};
}

float Window::refresh_rate() const {
    const auto *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(m_window));
    return mode != nullptr && mode->refresh_rate > 0 ? mode->refresh_rate : 0.0F;
}

std::filesystem::path Window::executable_directory() {
    const char *base = SDL_GetBasePath();
    std::u8string encoded;
    if (base != nullptr) {
        for (const char c : std::string_view(base)) {
            encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
        }
    }
    return std::filesystem::path(encoded);
}

std::expected<std::filesystem::path, Error> Window::preference_directory() {
    const std::unique_ptr<char, decltype(&SDL_free)> directory(
        SDL_GetPrefPath("microjump", "microjump"), SDL_free);
    if (!directory)
        return std::unexpected(
            Error{"Cannot locate saved expedition directory: " + std::string(SDL_GetError())});
    if (*directory == '\0')
        return std::unexpected(Error{"SDL_GetPrefPath returned an empty preference directory"});
    std::u8string encoded;
    for (const char c : std::string_view(directory.get()))
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    try {
        return std::filesystem::path(encoded);
    } catch (const std::filesystem::filesystem_error &error) {
        return std::unexpected(
            Error{"Cannot read saved expedition directory: " + std::string(error.what())});
    }
}

void Window::wait_for_events() const {
    // Minimized windows wait without consuming the next event.
    SDL_WaitEventTimeout(nullptr, 16);
}

} // namespace mj
