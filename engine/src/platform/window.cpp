#include <mj/platform/window.hpp>

#include <SDL3/SDL.h>

#include <cmath>

namespace mj {
namespace {
constexpr float k_stick_dead_zone = 0.18F;

float axis(SDL_Gamepad *gamepad, SDL_GamepadAxis which) {
    return std::clamp(static_cast<float>(SDL_GetGamepadAxis(gamepad, which)) / 32767.0F, -1.0F,
                      1.0F);
}
} // namespace

StickReading apply_dead_zone(StickReading raw, float dead_zone) {
    const float magnitude = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (magnitude <= dead_zone || magnitude <= 0)
        return {};
    const float scaled = std::min((magnitude - dead_zone) / (1 - dead_zone), 1.0F);
    return {raw.x / magnitude * scaled, raw.y / magnitude * scaled};
}

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
    return window;
}

Window::~Window() {
    if (m_gamepad != nullptr)
        SDL_CloseGamepad(m_gamepad);
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
                               bool capture_mouse) {
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
        } else if (event.type == SDL_EVENT_GAMEPAD_ADDED && m_gamepad == nullptr) {
            m_gamepad = SDL_OpenGamepad(event.gdevice.which);
            if (m_gamepad == nullptr)
                log(std::string("Gamepad open failed: ") + SDL_GetError());
            else
                log(std::string("Gamepad connected: ") + (SDL_GetGamepadName(m_gamepad) != nullptr
                                                              ? SDL_GetGamepadName(m_gamepad)
                                                              : "unnamed"));
        } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && m_gamepad != nullptr &&
                   event.gdevice.which == SDL_GetGamepadID(m_gamepad)) {
            SDL_CloseGamepad(m_gamepad);
            m_gamepad = nullptr;
            log("Gamepad disconnected");
        } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && m_gamepad != nullptr &&
                   event.gbutton.which == SDL_GetGamepadID(m_gamepad)) {
            const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
            input.jump_pressed = input.jump_pressed || button == SDL_GAMEPAD_BUTTON_SOUTH;
            input.reset_pressed = input.reset_pressed || button == SDL_GAMEPAD_BUTTON_BACK;
            input.toggle_tuning = input.toggle_tuning || button == SDL_GAMEPAD_BUTTON_START;
        }
    }
    const auto flags = SDL_GetWindowFlags(m_window);
    const bool focused =
        (flags & SDL_WINDOW_INPUT_FOCUS) != 0 && (flags & SDL_WINDOW_MINIMIZED) == 0;
    const bool capture = focused && capture_mouse;
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
    if (!focused)
        return input;
    const bool *keys = SDL_GetKeyboardState(nullptr);
    auto key = [keys](SDL_Scancode code) { return keys[code] ? 1.0F : 0.0F; };
    const float right = key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A);
    const float forward = key(SDL_SCANCODE_W) - key(SDL_SCANCODE_S);
    input.camera.strafe = right;
    input.camera.forward = forward;
    input.camera.vertical = key(SDL_SCANCODE_E) - key(SDL_SCANCODE_Q);
    input.camera.fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    input.jump_held = keys[SDL_SCANCODE_SPACE];
    input.walk_held = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
    const float keyboard_length = std::sqrt(right * right + forward * forward);
    if (keyboard_length > 0) {
        input.move_x = right / keyboard_length;
        input.move_y = forward / keyboard_length;
    }
    if (m_gamepad != nullptr) {
        input.gamepad_connected = true;
        // SDL sticks report +Y down; gameplay uses +Y forward / up.
        const auto move = apply_dead_zone(
            {axis(m_gamepad, SDL_GAMEPAD_AXIS_LEFTX), -axis(m_gamepad, SDL_GAMEPAD_AXIS_LEFTY)},
            k_stick_dead_zone);
        if (move.x * move.x + move.y * move.y >
            input.move_x * input.move_x + input.move_y * input.move_y) {
            input.move_x = move.x;
            input.move_y = move.y;
        }
        const auto look = apply_dead_zone(
            {axis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHTX), axis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHTY)},
            k_stick_dead_zone);
        input.look_stick_x = look.x;
        input.look_stick_y = look.y;
        input.jump_held =
            input.jump_held || SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    }
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

void Window::wait_for_events() const {
    // Minimized windows wait without consuming the next event.
    SDL_WaitEventTimeout(nullptr, 16);
}

} // namespace mj
