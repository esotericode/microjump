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
        SDL_CreateWindow("microjump — M0 foundation", 1280, 720,
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

bool Window::poll_quit() {
    SDL_Event event;
    bool quit = false;
    while (SDL_PollEvent(&event)) {
        quit = quit || event.type == SDL_EVENT_QUIT ||
               event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
               (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
    }
    return quit;
}

void Window::wait_for_events() const {
    // M0 has no renderer/simulation yet. Wait without consuming the next event.
    SDL_WaitEventTimeout(nullptr, 16);
}

} // namespace mj
