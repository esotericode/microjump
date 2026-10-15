#include <mj/platform/window.hpp>

#include <SDL3/SDL.h>

namespace mj {

std::expected<std::unique_ptr<Window>, Error> Window::create() {
    auto window = std::unique_ptr<Window>(new Window);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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

InputState Window::poll_events(const std::function<void(const SDL_Event &)> &handler) {
    SDL_Event event;
    InputState input;
    while (SDL_PollEvent(&event)) {
        handler(event);
        input.quit = input.quit || event.type == SDL_EVENT_QUIT ||
                     event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                     (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            input.toggle_overlay = input.toggle_overlay || event.key.key == SDLK_F1;
            input.reset_camera = input.reset_camera || event.key.key == SDLK_HOME;
        }
    }
    const auto flags = SDL_GetWindowFlags(m_window);
    const bool focused =
        (flags & SDL_WINDOW_INPUT_FOCUS) != 0 && (flags & SDL_WINDOW_MINIMIZED) == 0;
    const bool capture = focused && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK) != 0;
    const bool changed = capture != m_mouse_captured;
    if (changed) {
        if (!SDL_SetWindowRelativeMouseMode(m_window, capture))
            log(SDL_GetError());
        else
            m_mouse_captured = capture;
    }
    float dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);
    if (capture && !changed) {
        input.look_x = dx;
        input.look_y = dy;
    }
    if (focused) {
        const bool *keys = SDL_GetKeyboardState(nullptr);
        input.camera.strafe =
            static_cast<float>(keys[SDL_SCANCODE_D]) - static_cast<float>(keys[SDL_SCANCODE_A]);
        input.camera.forward =
            static_cast<float>(keys[SDL_SCANCODE_W]) - static_cast<float>(keys[SDL_SCANCODE_S]);
        input.camera.vertical =
            static_cast<float>(keys[SDL_SCANCODE_E]) - static_cast<float>(keys[SDL_SCANCODE_Q]);
        input.camera.fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
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
