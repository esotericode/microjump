#include <mj/core/launch_options.hpp>
#include <mj/platform/window.hpp>
#include <mj/render/vulkan_context.hpp>

#include <SDL3/SDL_main.h>

#include <string_view>
#include <vector>

namespace mj::game {

int run(const LaunchOptions &options) {
    ValidationLog validation;
    // Window outlives Vulkan surface; validation log outlives both.
    {
        auto window = Window::create();
        if (!window) {
            log(window.error().message);
            return 1;
        }
        auto context = VulkanContext::create(**window, validation);
        if (!context) {
            log(context.error().message);
            return 1;
        }
        log("M0 ready: window + Vulkan device; rendering begins in M1. Escape closes the window.");
        int event_cycles = 0;
        while (!(*window)->poll_quit()) {
            if (options.smoke_test && ++event_cycles >= 12) {
                break;
            }
            (*window)->wait_for_events();
        }
    }
    const auto errors = validation.error_count.load(std::memory_order_relaxed);
    log("Shutdown complete; Vulkan validation errors: " + std::to_string(errors));
    return errors == 0 ? 0 : 1;
}

} // namespace mj::game

int main(int argc, char *argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto options = mj::parse_launch_options(arguments);
    if (!options) {
        mj::log(options.error().message);
        return 1;
    }
    if (options->help) {
        mj::log("Usage: microjump [--help] [--smoke-test]\n"
                "  --smoke-test  Open the window, initialize Vulkan, process events, then exit.\n"
                "  Debug builds require Vulkan validation layers.\n"
                "  M0 does not draw a scene yet.");
        return 0;
    }
    return mj::game::run(*options);
}
