#include "scene.hpp"
#include <mj/core/fixed_step.hpp>
#include <mj/core/launch_options.hpp>
#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include <SDL3/SDL_main.h>

#include <chrono>
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
        auto renderer = Renderer::create(**window, **context, make_scene());
        if (!renderer) {
            log(renderer.error().message);
            return 1;
        }
        DebugCamera camera;
        FixedStep fixed;
        bool show_overlay = true;
        bool first_resize = false, second_resize = false;
        bool captured = false;
        std::uint32_t rendered = 0;
        std::optional<std::uint32_t> scripted_frame;
        auto last = std::chrono::steady_clock::now();
        log("M1 ready: WASD + Q/E fly, hold RMB to look, Shift fast, Home reset, F1 overlay.");
        while (true) {
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); });
            if (input.quit)
                break;
            if (input.toggle_overlay)
                show_overlay = !show_overlay;
            if (input.reset_camera)
                camera.reset();
            if (options.resize_test &&
                ((!first_resize && rendered == 4) || (!second_resize && rendered == 8))) {
                const bool first = !first_resize;
                auto resized = (*window)->resize(first ? 960 : 1280, first ? 640 : 800);
                if (!resized) {
                    log(resized.error().message);
                    return 1;
                }
                if (first)
                    first_resize = true;
                else
                    second_resize = true;
            }
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - last).count();
            last = now;
            if ((*window)->pixel_size().width == 0) {
                (*window)->wait_for_events();
                continue;
            }
            auto camera_input = input.camera;
            double simulation_elapsed = elapsed;
            if (options.camera_test) {
                simulation_elapsed = 0;
                if (scripted_frame != rendered) {
                    camera.look(-2.5F, -0.35F);
                    camera_input.strafe = -0.65F;
                    camera_input.forward = 1;
                    simulation_elapsed = 1.0 / 60;
                    scripted_frame = rendered;
                }
            } else
                camera.look(input.look_x, input.look_y);
            const float alpha =
                fixed.advance(simulation_elapsed, [&](float dt) { camera.tick(camera_input, dt); });
            const bool capture = options.capture_frame && rendered + 1 == *options.capture_frame;
            auto result = (*renderer)->draw(camera.interpolated(alpha), elapsed, show_overlay,
                                            capture ? &options.capture_out : nullptr);
            if (!result) {
                log(result.error().message);
                return 1;
            }
            if (!*result)
                continue;
            ++rendered;
            if (capture) {
                captured = true;
                break;
            }
            if (options.frames && rendered >= *options.frames)
                break;
        }
        if (options.frames || options.capture_frame)
            log("Rendered frames: " + std::to_string(rendered));
        if (options.capture_frame && !captured) {
            log("Window closed before the requested capture");
            return 1;
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
        mj::log("Usage: microjump [--frames N] [--capture-frame N --capture-out PATH.png]\n"
                "  --smoke-test  Render 12 frames and exit.\n"
                "  Debug builds require Vulkan validation layers.\n"
                "  --resize-test / --camera-test  Scripted render correctness checks.\n"
                "  WASD + Q/E fly, RMB look, Shift fast, Home reset, F1 overlay, Escape exit.");
        return 0;
    }
    return mj::game::run(*options);
}
