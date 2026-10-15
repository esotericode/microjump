#include "debug_ui.hpp"
#include "level.hpp"
#include "simulation.hpp"

#include <mj/core/fixed_step.hpp>
#include <mj/core/input_record.hpp>
#include <mj/core/launch_options.hpp>
#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include <SDL3/SDL_main.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

namespace mj::game {
namespace {
constexpr MeshHandle k_world_mesh{0};
constexpr MeshHandle k_player_mesh{1};

std::string describe(const Simulation &simulation) {
    const auto &s = simulation.player.current();
    std::array<char, 256> text{};
    std::snprintf(text.data(), text.size(),
                  "tick %llu, feet (%.9g, %.9g, %.9g), state %s, %s, checkpoint %zu",
                  static_cast<unsigned long long>(simulation.ticks()),
                  static_cast<double>(s.position.x), static_cast<double>(s.position.y),
                  static_cast<double>(s.position.z), to_string(s.state),
                  s.grounded ? "grounded" : "airborne", simulation.active_checkpoint());
    return text.data();
}
} // namespace

int run(const LaunchOptions &options) {
    ValidationLog validation;
    const bool replaying = !options.replay.empty();
    std::vector<TickInput> replay;
    if (replaying) {
        auto loaded = load_replay(options.replay);
        if (!loaded) {
            log(loaded.error().message);
            return 1;
        }
        replay = std::move(*loaded);
        log("Replaying " + std::to_string(replay.size()) + " ticks from " +
            options.replay.string());
    }
    const Level level = make_movement_yard();
    const LevelGeometry geometry = build_geometry(level);
    Simulation simulation(level, geometry.collision);
    std::vector<TickInput> recording;
    std::size_t replay_index = 0;
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
        const std::array meshes{geometry.mesh, make_player_mesh(simulation.player.tuning.radius,
                                                                simulation.player.tuning.height)};
        auto renderer = Renderer::create(**window, **context, meshes);
        if (!renderer) {
            log(renderer.error().message);
            return 1;
        }
        DebugCamera free_camera;
        FixedStep fixed;
        bool show_overlay = true;
        bool tuning_open = false;
        bool use_free_camera = options.camera_test;
        bool first_resize = false, second_resize = false;
        bool captured = false;
        bool replay_finished = replaying && replay.empty();
        std::uint32_t pending_buttons = 0; // Press edges latched until the next tick.
        std::uint32_t rendered = 0;
        std::optional<std::uint32_t> scripted_frame;
        SessionStatus status;
        auto last = std::chrono::steady_clock::now();
        log("M2 ready: WASD move, Space jump, mouse look, R respawn, F1 overlay, F2 tuning, "
            "F3 free camera.");
        while (true) {
            const bool capture_mouse = !tuning_open && !options.camera_test;
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); },
                capture_mouse && !(*renderer)->ui_wants_mouse());
            if (input.quit)
                break;
            if (input.toggle_overlay)
                show_overlay = !show_overlay;
            if (input.toggle_tuning)
                tuning_open = !tuning_open;
            if (input.toggle_free_camera && !options.camera_test) {
                use_free_camera = !use_free_camera;
                if (use_free_camera)
                    free_camera.reset();
            }
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

            CameraInput camera_input;
            double simulation_elapsed = elapsed;
            TickInput live;
            if (use_free_camera) {
                camera_input = input.camera;
                if (options.camera_test) {
                    simulation_elapsed = 0;
                    if (scripted_frame != rendered) {
                        free_camera.look(-2.5F, -0.35F);
                        camera_input.strafe = -0.65F;
                        camera_input.forward = 1;
                        simulation_elapsed = 1.0 / 60;
                        scripted_frame = rendered;
                    }
                } else {
                    free_camera.look(input.look_x, input.look_y);
                    if (input.reset_pressed)
                        free_camera.reset();
                }
            } else if (!replaying) {
                simulation.camera.look({input.look_x, input.look_y, input.look_stick_x,
                                        input.look_stick_y,
                                        static_cast<float>(std::min(elapsed, 0.1))});
                const float scale = input.walk_held ? simulation.player.tuning.walk_fraction : 1;
                live.move_x = input.move_x * scale;
                live.move_y = input.move_y * scale;
                if (input.jump_held)
                    live.buttons |= input_button::jump_held;
                if (input.jump_pressed)
                    pending_buttons |= input_button::jump_pressed;
                if (input.reset_pressed)
                    pending_buttons |= input_button::reset_pressed;
            }
            const float alpha = fixed.advance(simulation_elapsed, [&](float dt) {
                if (use_free_camera)
                    free_camera.tick(camera_input, dt);
                TickInput tick;
                if (replaying) {
                    if (replay_index >= replay.size()) {
                        replay_finished = true;
                        return;
                    }
                    tick = replay[replay_index++];
                } else {
                    if (!use_free_camera) { // The player stands still while the camera flies.
                        tick = live;
                        tick.buttons |= pending_buttons;
                        pending_buttons = 0;
                    }
                    // Live ticks use the orientation the player is looking with right now.
                    tick.camera_yaw = simulation.camera.yaw();
                    tick.camera_pitch = simulation.camera.pitch();
                }
                if (!options.record.empty())
                    recording.push_back(tick);
                simulation.tick(tick, dt);
            });

            const auto pose = simulation.player.interpolated(alpha);
            const std::array instances{
                DrawInstance{k_world_mesh, yaw_translation(0, {}), 1, true},
                DrawInstance{k_player_mesh, yaw_translation(pose.facing, pose.position), 0, false}};
            status.free_camera = use_free_camera;
            status.gamepad = input.gamepad_connected;
            status.input_mode = replaying ? "Replay " + std::to_string(replay_index) + "/" +
                                                std::to_string(replay.size())
                                : !options.record.empty() ? "Recording"
                                                          : "Live";
            const std::function<void()> overlay_lines = [&] {
                movement_overlay(simulation, status);
            };
            const std::function<void()> debug_windows = [&] {
                tuning_window(simulation, tuning_open);
            };
            const bool capture = options.capture_frame && rendered + 1 == *options.capture_frame;
            FrameView view;
            view.camera = use_free_camera ? free_camera.interpolated(alpha)
                                          : simulation.camera.pose(alpha, geometry.collision);
            view.instances = instances;
            view.blob = {pose.position, simulation.player.tuning.radius * 1.35F};
            view.elapsed = elapsed;
            view.overlay = show_overlay;
            view.overlay_lines = &overlay_lines;
            view.debug_windows = &debug_windows;
            view.capture = capture ? &options.capture_out : nullptr;
            auto result = (*renderer)->draw(view);
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
            if (replay_finished)
                break;
        }
        if (options.frames || options.capture_frame)
            log("Rendered frames: " + std::to_string(rendered));
        if (options.capture_frame && !captured) {
            log("Window closed before the requested capture");
            return 1;
        }
    }
    if (replaying)
        log("Replay " + std::string(replay_index >= replay.size() ? "finished" : "stopped") + ": " +
            describe(simulation));
    if (!options.record.empty()) {
        if (auto saved = save_replay(options.record, recording); !saved) {
            log(saved.error().message);
            return 1;
        }
        log("Recorded " + std::to_string(recording.size()) + " ticks to " +
            options.record.string() + ": " + describe(simulation));
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
                "                 [--record PATH | --replay PATH]\n"
                "  --smoke-test  Render 12 frames and exit.\n"
                "  --record PATH  Save every simulation tick's input on exit.\n"
                "  --replay PATH  Play recorded input instead of live input, then exit.\n"
                "  --resize-test / --camera-test  Scripted render correctness checks.\n"
                "  Debug builds require Vulkan validation layers.\n"
                "  WASD move, Space jump, Ctrl walk, mouse look, R respawn, F1 overlay,\n"
                "  F2 tuning, F3 free camera, Escape exit. Gamepad: sticks, A jump, Back respawn.");
        return 0;
    }
    return mj::game::run(*options);
}
