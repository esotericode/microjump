#include "debug_ui.hpp"
#include "level.hpp"
#include "level_io.hpp"
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
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mj::game {
namespace {
constexpr MeshHandle k_world_mesh{0};
constexpr MeshHandle k_player_mesh{1};
constexpr MeshHandle k_bulb_mesh{2};

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
    std::string replay_level;
    if (replaying) {
        auto loaded = load_replay_file(options.replay);
        if (!loaded) {
            log(loaded.error().message);
            return 1;
        }
        replay = std::move(loaded->ticks);
        replay_level = std::move(loaded->level);
        if (options.capture_tick && *options.capture_tick > replay.size()) {
            log("Capture tick exceeds the replay length");
            return 1;
        }
        log("Replaying " + std::to_string(replay.size()) + " ticks from " +
            options.replay.string());
    }
    const auto level_path = options.level.empty()
                                ? Window::executable_directory() / "assets/levels/playground.json"
                                : options.level;
    auto loaded_level = load_level(level_path);
    if (!loaded_level) {
        log(loaded_level.error().message);
        return 1;
    }
    const Level level = std::move(*loaded_level);
    log("Level: " + level.name + " (" + std::to_string(level.shapes.size()) + " primitives, " +
        std::to_string(level.checkpoints.size()) + " checkpoints)");
    if (replaying && !replay_level.empty() && replay_level != level.name) {
        log("Replay was recorded on level '" + replay_level + "' but '" + level.name +
            "' is loaded; pass --level with that level's file");
        return 1;
    }
    if (replaying && replay_level.empty())
        log("Replay predates level names (format 1/2); it assumes the loaded level '" + level.name +
            "'");
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
        // Order matches k_world_mesh, k_player_mesh, k_bulb_mesh.
        const std::array meshes{
            geometry.mesh,
            make_player_mesh(simulation.player.tuning.radius, simulation.player.tuning.height),
            make_bulb_mesh()};
        auto renderer = Renderer::create(**window, **context, meshes);
        if (!renderer) {
            log(renderer.error().message);
            return 1;
        }
        DebugCamera free_camera;
        FixedStep fixed;
        bool show_overlay = !options.hide_overlay;
        bool tuning_open = false;
        bool use_free_camera = options.camera_test;
        bool first_resize = false, second_resize = false;
        bool captured = false;
        bool replay_finished = replaying && replay.empty();
        std::uint32_t pending_buttons = 0; // Press edges latched until the next tick.
        std::uint32_t rendered = 0;
        std::optional<std::uint32_t> scripted_frame;
        SessionStatus status;
        FramePacing pacing;
        status.pacing = &pacing;
        DiagnosticsControls diagnostics;
        diagnostics.display_pacing_supported = (*renderer)->display_pacing_supported();
        diagnostics.refresh_hz = (*window)->refresh_rate();
        log("Display refresh: " + std::format("{:.2f}", diagnostics.refresh_hz) + " Hz");
        const auto start_frame_log = [&](const std::filesystem::path &path) {
            if (pacing.open_log(path))
                log("Frame log: " + path.string());
            else
                log("Cannot write frame log: " + path.string());
            diagnostics.logging = pacing.logging();
            diagnostics.log_path = pacing.logging() ? path.string() : std::string{};
        };
        if (!options.frame_log.empty())
            start_frame_log(options.frame_log);
        std::string events; // Pacing-log events since the last rendered frame.
        const auto note = [&events](std::string_view name) {
            if (!events.empty())
                events.push_back(';');
            events.append(name);
        };
        bool was_focused = true;
        bool was_captured = false;
        auto last = std::chrono::steady_clock::now();
        LightingControls lighting;
        if (options.time_of_day)
            lighting.cycle.hours = *options.time_of_day;
        if (options.shadow_quality)
            lighting.shadows.quality = *options.shadow_quality;
        // --pacing-test: each phase runs 5 s; the last 4 s (after any rebuild hitch) are
        // summarized, so a lasting change in pacing shows up as a changed steady state.
        struct PacingPhase {
            const char *name;
            std::function<void()> enter;
        };
        const std::array<PacingPhase, 9> pacing_phases{{
            {"baseline (shadows Medium)", [] {}},
            {"shadows High", [&] { lighting.shadows.quality = ShadowQuality::high; }},
            {"shadows Low", [&] { lighting.shadows.quality = ShadowQuality::low; }},
            {"shadows Medium", [&] { lighting.shadows.quality = ShadowQuality::medium; }},
            {"GPU idle wait", [&] { (*renderer)->request(RenderAction::wait_idle); }},
            {"shadow map rebuilt (same quality)",
             [&] { (*renderer)->request(RenderAction::rebuild_shadow_map); }},
            {"swapchain recreated", [&] { (*renderer)->request(RenderAction::rebuild_swapchain); }},
            {"frame time snapped to refresh", [&] { diagnostics.snap_to_refresh = true; }},
            // Shows what the driver does without display pacing after the original trigger.
            {"display pacing off, GPU idle wait",
             [&] {
                 diagnostics.display_pacing.enabled = false;
                 (*renderer)->request(RenderAction::wait_idle);
             }},
        }};
        std::size_t pacing_phase = 0;
        auto phase_started = std::chrono::steady_clock::now();
        std::vector<FrameSample> phase_samples;
        if (options.pacing_test) {
            lighting.shadows.quality = ShadowQuality::medium;
            log(std::format("Pacing test: {} phases of 5 s; keep the window focused and visible",
                            pacing_phases.size()));
        }
        std::vector<DrawInstance> instances;
        log("M4 playground ready: WASD move, Space jump/wall kick, Shift+jump long jump, R "
            "respawn, F1 overlay, F2 "
            "tuning, "
            "F3 free camera.");
        while (true) {
            // Input and time are sampled once the display has caught up (docs/frame-pacing.md).
            if (auto paced = (*renderer)->wait_for_display(diagnostics.display_pacing); !paced) {
                log(paced.error().message);
                return 1;
            }
            // Replays ignore live look input, so they never grab the cursor.
            const bool capture_mouse = !tuning_open && !options.camera_test && !replaying;
            // While tuning is open a controller drives the menu, so its gameplay input is
            // withheld; the keyboard keeps moving the player unless ImGui is taking text.
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); },
                {.capture_mouse = capture_mouse && !(*renderer)->ui_wants_mouse(),
                 .gamepad_gameplay = !tuning_open});
            const bool gameplay_input = input.focused && !(*renderer)->ui_wants_keyboard();
            if (input.quit)
                break;
            if (input.toggle_overlay)
                show_overlay = !show_overlay;
            if (input.toggle_tuning) {
                tuning_open = !tuning_open;
                note(tuning_open ? "tuning_open" : "tuning_closed");
            }
            if (input.mouse_captured != was_captured) {
                note(input.mouse_captured ? "mouse_captured" : "mouse_released");
                was_captured = input.mouse_captured;
            }
            if (input.focused != was_focused) {
                note(input.focused ? "focus_gained" : "focus_lost");
                was_focused = input.focused;
            }
            if (!gameplay_input)
                pending_buttons = 0;
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

            if (const float refresh = (*window)->refresh_rate();
                refresh != diagnostics.refresh_hz) {
                diagnostics.refresh_hz = refresh;
                note(std::format("refresh_{:.2f}Hz", refresh));
            }
            CameraInput camera_input;
            // Optional experiment: remove timer jitter from a vsynced cadence.
            double simulation_elapsed =
                diagnostics.snap_to_refresh
                    ? snap_to_refresh(elapsed, {.refresh_hz = diagnostics.refresh_hz})
                    : elapsed;
            TickInput live;
            if (use_free_camera && (gameplay_input || options.camera_test)) {
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
            } else if (!replaying && gameplay_input) {
                if (input.recenter_pressed)
                    simulation.camera.set_orientation({simulation.player.current().facing,
                                                       simulation.camera.tuning.default_pitch});
                simulation.camera.look({input.look_x, input.look_y, input.look_stick_x,
                                        input.look_stick_y,
                                        static_cast<float>(std::min(elapsed, 0.1))});
                const float scale = input.walk_held ? simulation.player.tuning.walk_fraction : 1;
                live.move_x = input.move_x * scale;
                live.move_y = input.move_y * scale;
                if (input.long_jump_held)
                    live.buttons |= input_button::long_jump_held;
                if (input.jump_held)
                    live.buttons |= input_button::jump_held;
                if (input.jump_pressed) {
                    pending_buttons |= input_button::jump_pressed;
                    if (input.long_jump_held)
                        pending_buttons |= input_button::long_jump_held;
                }
                if (input.reset_pressed)
                    pending_buttons |= input_button::reset_pressed;
            }
            std::uint32_t frame_ticks = 0;
            const auto simulation_started = std::chrono::steady_clock::now();
            float alpha = fixed.advance(simulation_elapsed, [&](float dt) {
                // A capture is a simulation snapshot: don't run past its requested tick,
                // even when a slow rendered frame advances several simulation ticks.
                if (options.capture_tick && simulation.ticks() >= *options.capture_tick)
                    return;
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
                ++frame_ticks;
                lighting.cycle.advance(dt); // Visual only; never feeds back into gameplay.
            });
            const double simulation_ms = std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - simulation_started)
                                             .count();
            if (options.capture_tick && simulation.ticks() == *options.capture_tick)
                alpha = 1; // Show the selected state, independent of the render accumulator.

            const auto pose = simulation.player.interpolated(alpha);
            const float seconds =
                std::max(0.0F, static_cast<float>(simulation.ticks()) - 1 + alpha) *
                static_cast<float>(FixedStep::k_tick);
            const auto lights = lighting.local_lights
                                    ? animate_lights(level.lights, {seconds, lighting.cycle.hours})
                                    : std::vector<LocalLight>{};
            instances.clear();
            instances.push_back({.mesh = k_world_mesh,
                                 .model = yaw_translation(0, {}),
                                 .grid = 1,
                                 .receives_blob = true});
            instances.push_back(
                {.mesh = k_player_mesh, .model = yaw_translation(pose.facing, pose.position)});
            if (lighting.bulbs) {
                for (const auto &light : lights) {
                    const float peak = std::max({light.color.x, light.color.y, light.color.z});
                    if (peak <= 0)
                        continue;
                    instances.push_back(
                        {.mesh = k_bulb_mesh,
                         .model = yaw_translation(0, light.position),
                         .casts_shadow = false,
                         .tint = light.color * (1 / peak),
                         .emissive = std::min(peak, 2.5F)}); // Keeps hue after tone mapping.
                }
            }
            status.hours = lighting.cycle.hours;
            status.level = level.name;
            status.zone = zone_at(level, pose.position);
            status.day_animating = lighting.cycle.animate;
            status.free_camera = use_free_camera;
            status.gamepad_name = input.gamepad_name;
            status.gamepad_labels = input.gamepad_labels;
            status.input_mode = replaying ? "Replay " + std::to_string(replay_index) + "/" +
                                                std::to_string(replay.size())
                                : !options.record.empty() ? "Recording"
                                                          : "Live";
            const std::function<void()> overlay_lines = [&] {
                movement_overlay(simulation, status);
            };
            const std::function<void()> debug_windows = [&] {
                tuning_window(simulation, {lighting, (*window)->gamepad_tuning, diagnostics},
                              tuning_open);
            };
            if (options.pacing_test) {
                status.input_mode +=
                    std::format(" | Pacing test {}/{}: {}", pacing_phase + 1, pacing_phases.size(),
                                pacing_phases[pacing_phase].name);
                if (now - phase_started >= std::chrono::seconds(5)) {
                    log(std::format("Pacing phase {} ({}): {}", pacing_phase + 1,
                                    pacing_phases[pacing_phase].name,
                                    describe(summarize(phase_samples))));
                    phase_samples.clear();
                    phase_started = now;
                    if (++pacing_phase == pacing_phases.size())
                        break;
                    note(std::format("phase_{}", pacing_phase + 1));
                    pacing_phases[pacing_phase].enter();
                }
            }
            const bool capture =
                (options.capture_frame && rendered + 1 == *options.capture_frame) ||
                (options.capture_tick && !captured && simulation.ticks() >= *options.capture_tick);
            FrameView view;
            view.camera = use_free_camera ? free_camera.interpolated(alpha)
                                          : simulation.camera.pose(alpha, geometry.collision);
            view.instances = instances;
            view.blob = {pose.position,
                         lighting.blob ? simulation.player.tuning.radius * 1.35F : 0.0F,
                         lighting.blob_strength};
            view.environment = environment_at(lighting.cycle);
            view.lights = lights;
            view.shadows = lighting.shadows;
            view.shadows.focus = pose.position;
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
            const bool snapping = diagnostics.snap_to_refresh;
            if (diagnostics.wait_idle) {
                (*renderer)->request(RenderAction::wait_idle);
                note("action_wait_idle");
            }
            if (diagnostics.rebuild_shadow_map) {
                (*renderer)->request(RenderAction::rebuild_shadow_map);
                note("action_rebuild_shadow_map");
            }
            if (diagnostics.rebuild_swapchain) {
                (*renderer)->request(RenderAction::rebuild_swapchain);
                note("action_recreate_swapchain");
            }
            if (diagnostics.toggle_log) {
                if (pacing.logging()) {
                    pacing.close_log();
                    diagnostics.logging = false;
                    log("Frame log closed: " + diagnostics.log_path);
                } else {
                    start_frame_log(options.frame_log.empty()
                                        ? Window::executable_directory() / "frame-log.csv"
                                        : options.frame_log);
                }
            }
            diagnostics.wait_idle = diagnostics.rebuild_shadow_map = diagnostics.rebuild_swapchain =
                diagnostics.toggle_log = false;
            if (!*result)
                continue;
            ++rendered;
            {
                const auto &timing = (*renderer)->last_timing();
                if (timing.shadow_rebuilt)
                    note("shadow_rebuilt_" + std::string(to_string(lighting.shadows.quality)));
                if (timing.swapchain_rebuilt)
                    note("swapchain_rebuilt");
                if (timing.suboptimal)
                    note("suboptimal");
                if (timing.display_timeout)
                    note("display_wait_timeout");
                if (snapping)
                    note("snap");
                const FrameSample sample{.delta_ms = elapsed * 1000,
                                         .sim_delta_ms = simulation_elapsed * 1000,
                                         .ticks = frame_ticks,
                                         .alpha = alpha,
                                         .sim_cpu_ms = simulation_ms,
                                         .display_wait_ms = timing.display_wait_ms,
                                         .rebuild_ms = timing.rebuild_ms,
                                         .wait_ms = timing.wait_ms,
                                         .acquire_ms = timing.acquire_ms,
                                         .record_ms = timing.record_ms,
                                         .present_ms = timing.present_ms,
                                         .gpu_ms = timing.gpu_ms,
                                         .image = timing.image,
                                         .mouse_dx = input.look_x,
                                         .mouse_dy = input.look_y,
                                         .events = events};
                pacing.add(sample);
                if (options.pacing_test && now - phase_started >= std::chrono::seconds(1))
                    phase_samples.push_back(sample);
                events.clear();
            }
            if (capture) {
                log("Capture state: " + describe(simulation));
                captured = true;
                break;
            }
            if (options.frames && rendered >= *options.frames)
                break;
            if (replay_finished)
                break;
        }
        if (options.frames || options.capture_frame || options.capture_tick)
            log("Rendered frames: " + std::to_string(rendered));
        if ((options.capture_frame || options.capture_tick) && !captured) {
            log("Window closed before the requested capture");
            return 1;
        }
    }
    if (replaying)
        log("Replay " + std::string(replay_index >= replay.size() ? "finished" : "stopped") + ": " +
            describe(simulation));
    if (!options.record.empty()) {
        if (auto saved = save_replay(options.record, recording, level.name); !saved) {
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
        mj::log(
            "Usage: microjump [--frames N] [--capture-frame N --capture-out PATH.png]\n"
            "                 [--record PATH | --replay PATH]\n"
            "  --smoke-test  Render 12 frames and exit.\n"
            "  --record PATH  Save every simulation tick's input on exit.\n"
            "  --replay PATH  Play recorded input instead of live input, then exit.\n"
            "  --level PATH  Load a version-1 JSON level; default is the packaged playground.\n"
            "  --time-of-day H  Start at H hours (0-24; 12 noon). Default 14.5.\n"
            "  --shadow-quality low|medium|high  Sun shadow preset. Default medium.\n"
            "  --hide-overlay  Start with the F1 overlay hidden (clean captures).\n"
            "  --capture-tick N  Capture the exact state at simulation tick N.\n"
            "  --frame-log PATH  Write per-frame timing (CSV) for frame-pacing diagnostics.\n"
            "  --resize-test / --camera-test  Scripted render correctness checks.\n"
            "  Debug builds require Vulkan validation layers.\n"
            "  WASD move, Space jump/wall kick, Shift+jump long jump, Ctrl walk, mouse look, R "
            "respawn, F1 overlay,\n"
            "  F2 tuning, F3 free camera, Escape exit. Gamepad: sticks, South jump, LB+jump long "
            "jump, R3 center, Back respawn, Start tuning.");
        return 0;
    }
    return mj::game::run(*options);
}
