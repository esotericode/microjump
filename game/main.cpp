#include "character.hpp"
#include "debug_ui.hpp"
#include "level.hpp"
#include "level_io.hpp"
#include "pause_menu.hpp"
#include "portal_visuals.hpp"
#include "progress.hpp"
#include "simulation.hpp"
#include "sound.hpp"
#include "spark_visuals.hpp"

#include <mj/audio/audio_output.hpp>
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mj::game {
namespace {
constexpr MeshHandle k_world_mesh{0};
constexpr MeshHandle k_player_mesh{1};
constexpr MeshHandle k_bulb_mesh{2};
constexpr MeshHandle k_spark_mesh{3};
constexpr MeshHandle k_matte_part_mesh{4};
constexpr MeshHandle k_glossy_part_mesh{5};
constexpr MeshHandle k_portal_ring_mesh{6};
constexpr std::uint32_t k_first_mover_mesh = 7; // The level's movers follow, one mesh each.

// The level being played, its geometry and the simulation that references both. It is held
// by pointer and never moves, so traveling through a gate swaps in a whole new one.
struct LoadedLevel {
    LoadedLevel(std::filesystem::path file, Level loaded, std::size_t checkpoint = 0)
        : path(std::move(file)), level(std::move(loaded)), geometry(build_geometry(level)),
          simulation(level, geometry.collision, checkpoint) {}
    std::filesystem::path path;
    Level level;
    LevelGeometry geometry;
    Simulation simulation;
};

// Live progress is independent of input recordings and simulation ticks. Even with
// persistence disabled it retains each world's finds while traveling this session.
struct Expedition {
    Progress saved;
    ProgressControls controls;
    std::filesystem::path path;
    bool writable = true;
    bool dirty = false;
    std::chrono::steady_clock::time_point noted;

    void message(std::string text, bool error = false) {
        controls.message = std::move(text);
        controls.error = error;
        noted = std::chrono::steady_clock::now();
    }

    void open(const LaunchOptions &options) {
        if (!uses_saved_progress(options))
            return;
        controls.enabled = true;
        path = options.save;
        if (path.empty()) {
            auto directory = Window::preference_directory();
            if (!directory) {
                log("Progress could not be loaded: " + directory.error().message);
                message("Saving is unavailable. Your expedition lasts until you close the game.",
                        true);
                controls.enabled = false;
                writable = false;
                return;
            }
            path = *directory / "progress.json";
        }
        auto loaded = load_progress(path);
        if (!loaded) {
            log("Progress could not be loaded: " + loaded.error().message);
            message("Saved progress could not be read. The file is preserved; start a new "
                    "expedition in F2 to replace it.",
                    true);
            writable = false;
            return;
        }
        saved = std::move(*loaded);
        message(saved.levels.empty() ? "A new expedition. Progress saves automatically."
                                     : "Expedition resumed. Progress saves automatically.");
    }

    bool remember(const LoadedLevel &world) {
        std::string file;
        try {
            for (const char8_t byte : world.path.filename().u8string())
                file.push_back(static_cast<char>(byte));
        } catch (const std::filesystem::filesystem_error &) {
            file.clear(); // Keep named progress even if a native filename cannot be encoded.
        }
        if (!valid_level_file(file))
            file.clear(); // An explicitly selected custom file need not be a gate filename.
        const auto result =
            remember_progress(saved, file, world.level, world.simulation.active_checkpoint(),
                              world.simulation.collected());
        if (!result) {
            if (controls.enabled) {
                log("Progress could not be saved: " + result.error().message);
                message("Progress could not be saved. Your previous save is preserved.", true);
            }
            return false;
        }
        dirty = true;
        return true;
    }

    void flush() {
        if (!controls.enabled || !writable || !dirty || path.empty())
            return;
        const auto result = save_progress(path, saved);
        if (!result) {
            log("Progress could not be saved: " + result.error().message);
            message("Progress could not be saved. Your previous save is preserved.", true);
            return;
        }
        dirty = false;
        log("Progress saved");
        message("Progress saved");
    }

    void describe(std::string_view phase, const LoadedLevel &world) const {
        if (controls.enabled)
            log(std::format("Expedition {}: level {}, checkpoint {}, sparks {}/{}", phase,
                            world.level.name, world.simulation.active_checkpoint(),
                            world.simulation.collected_count(),
                            world.simulation.collectible_count()));
    }
};

std::string describe(const Simulation &simulation) {
    const auto &s = simulation.player.current();
    std::array<char, 256> text{};
    std::snprintf(
        text.data(), text.size(),
        "tick %llu, feet (%.9g, %.9g, %.9g), state %s, %s, checkpoint %zu, sparks %zu/%zu",
        static_cast<unsigned long long>(simulation.ticks()), static_cast<double>(s.position.x),
        static_cast<double>(s.position.y), static_cast<double>(s.position.z), to_string(s.state),
        s.grounded ? "grounded" : "airborne", simulation.active_checkpoint(),
        simulation.collected_count(), simulation.collectible_count());
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
    Expedition expedition;
    expedition.open(options);
    const auto starting_path =
        options.level.empty() ? Window::executable_directory() / "assets/levels/playground.json"
                              : options.level;
    auto level_path = starting_path;
    if (options.level.empty() && !expedition.saved.last_level.empty())
        level_path = starting_path.parent_path() / expedition.saved.last_level;
    auto loaded_level = load_level(level_path);
    if (!loaded_level && level_path != starting_path) {
        log("Saved world unavailable; returning to the playground: " +
            loaded_level.error().message);
        level_path = starting_path;
        loaded_level = load_level(level_path);
    }
    if (!loaded_level) {
        log(loaded_level.error().message);
        return 1;
    }
    const auto describe_level = [](const Level &level) {
        log("Level: " + level.name + " (" + std::to_string(level.shapes.size()) + " primitives, " +
            std::to_string(level.checkpoints.size()) + " checkpoints)");
    };
    describe_level(*loaded_level);
    if (replaying && !replay_level.empty() && replay_level != loaded_level->name) {
        log("Replay was recorded on level '" + replay_level + "' but '" + loaded_level->name +
            "' is loaded; pass --level with that level's file");
        return 1;
    }
    if (replaying && replay_level.empty())
        log("Replay predates level names (format 1/2); it assumes the loaded level '" +
            loaded_level->name + "'");
    const auto restored = restore_progress(expedition.saved, *loaded_level);
    auto world =
        std::make_unique<LoadedLevel>(level_path, std::move(*loaded_level), restored.checkpoint);
    world->simulation.restore_collected(restored.collected);
    expedition.describe("start", *world);
    // Gates only work in live play: a recording or replay belongs to one level.
    const bool travel_allowed = !replaying && options.record.empty() && !options.camera_test;
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
        // Order matches the k_*_mesh handles above.
        std::vector meshes{world->geometry.mesh,
                           make_player_mesh(world->simulation.player.tuning.radius,
                                            world->simulation.player.tuning.height),
                           make_bulb_mesh(),
                           make_spark_mesh(),
                           make_part_mesh(PartMesh::matte),
                           make_part_mesh(PartMesh::glossy),
                           make_portal_ring_mesh()};
        meshes.insert(meshes.end(), world->geometry.mover_meshes.begin(),
                      world->geometry.mover_meshes.end());
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
        lighting.cycle.hours =
            options.time_of_day ? *options.time_of_day : world->level.atmosphere.time_of_day;
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
        CharacterAnimator character;
        character.snap(world->simulation.player.current());
        // Sound is presentation, like the character: a silent run plays exactly the same.
        std::unique_ptr<audio::Mixer> mixer;
        std::unique_ptr<audio::AudioOutput> speaker; // Feeds the mixer; closes first.
        AudioControls audio_controls;
        if (uses_audio(options)) {
            mixer = std::make_unique<audio::Mixer>(make_sound_bank());
            if (auto opened = audio::AudioOutput::open(*mixer)) {
                speaker = std::move(*opened);
                audio_controls.available = true;
                audio_controls.driver = speaker->driver();
                log("Audio: " + speaker->driver());
            } else {
                log(opened.error().message + "; playing silently");
                mixer.reset();
            }
        }
        SoundDirector sound;
        sound.reset(world->simulation);
        std::vector<CueEvent> cues;
        std::vector<CharacterPart> character_parts;
        std::vector<ParticleInstance> particles;
        std::optional<std::size_t> pending_travel; // A gate entered during the last frame.
        std::optional<std::chrono::steady_clock::time_point> arrived;
        std::uint32_t progress_test_ticks = 0; // Counts across gate-induced simulation swaps.
        const auto replace_world =
            [&](std::unique_ptr<LoadedLevel> next) -> std::expected<void, Error> {
            meshes[k_world_mesh.index] = next->geometry.mesh;
            meshes.resize(k_first_mover_mesh);
            meshes.insert(meshes.end(), next->geometry.mover_meshes.begin(),
                          next->geometry.mover_meshes.end());
            if (auto uploaded = (*renderer)->set_meshes(meshes); !uploaded)
                return uploaded;
            world = std::move(next);
            fixed = {};
            pending_buttons = 0;
            character.snap(world->simulation.player.current());
            sound.reset(world->simulation);
            if (!options.time_of_day)
                lighting.cycle.hours = world->level.atmosphere.time_of_day;
            return {};
        };
        // Swaps in another level, keeping its finds, at `checkpoint` or else where its saved
        // expedition last stood. Gates and the pause menu both arrive this way.
        const auto arrive = [&](const std::filesystem::path &path, Level loaded,
                                std::optional<std::size_t> checkpoint,
                                const std::string &how) -> std::expected<void, Error> {
            describe_level(loaded);
            expedition.remember(*world);
            const auto kept = restore_progress(expedition.saved, loaded);
            auto next = std::make_unique<LoadedLevel>(path, std::move(loaded),
                                                      checkpoint.value_or(kept.checkpoint));
            next->simulation.restore_collected(kept.collected);
            if (auto replaced = replace_world(std::move(next)); !replaced)
                return replaced;
            if (expedition.remember(*world))
                expedition.flush();
            arrived = std::chrono::steady_clock::now();
            cues.push_back({Cue::gate});
            log("Traveled " + how + " to " + world->level.name);
            return {};
        };
        // Swaps in the gate's level at its arrival checkpoint. A level that fails to load
        // keeps you where you are; only a renderer failure ends the game.
        const auto travel = [&](std::size_t gate) -> std::expected<void, Error> {
            const Portal portal = world->level.portals[gate];
            const auto path = world->path.parent_path() / portal.level;
            auto loaded = load_level(path);
            if (loaded && portal.checkpoint >= loaded->checkpoints.size())
                loaded = std::unexpected(
                    Error{"it has no checkpoint " + std::to_string(portal.checkpoint)});
            if (!loaded) {
                log("The gate '" + portal.name + "' is closed: " + loaded.error().message);
                return {};
            }
            // The gate owns the arrival point. A saved checkpoint is used only otherwise.
            return arrive(path, std::move(*loaded), portal.checkpoint,
                          "through '" + portal.name + "'");
        };
        // The pause menu: places to travel to, the player's options, and a quit that asks.
        const bool interactive = is_interactive(options);
        const auto places = interactive || options.show_menu ? discover_places(starting_path)
                                                             : std::vector<Place>{};
        PauseMenu menu;
        if (options.show_menu)
            menu.open(*options.show_menu);
        MenuChoice choice;
        PlayerOptions player_options;
        bool was_paused = menu.is_open();
        PlayerOptions applied_options;
        const Simulation *applied_to = nullptr; // A new world starts with default camera tuning.
        std::vector<PlaceStatus> place_status;
        log("Ready: WASD move, Space jump (again on landing: double, triple)/wall kick, "
            "Shift+jump long jump, E or click dive (jump to roll out), R respawn, F1 overlay, "
            "F2 tuning, F3 free camera, Escape pause.");
        expedition.controls.reset_requested = options.progress_test == ProgressTest::reset;
        while (true) {
            if (expedition.controls.reset_requested) {
                expedition.controls.reset_requested = expedition.controls.confirm_reset = false;
                auto fresh = load_level(starting_path);
                if (!fresh) {
                    log("New expedition could not start: " + fresh.error().message);
                    expedition.message(
                        "The starting level could not be opened. Your expedition is preserved.",
                        true);
                } else {
                    auto next = std::make_unique<LoadedLevel>(starting_path, std::move(*fresh));
                    if (auto replaced = replace_world(std::move(next)); !replaced) {
                        log(replaced.error().message);
                        return 1;
                    }
                    expedition.saved = {};
                    expedition.writable = !expedition.path.empty();
                    pending_travel.reset();
                    arrived.reset();
                    use_free_camera = false;
                    expedition.message("A new expedition. Your hunt starts here.");
                    if (expedition.remember(*world))
                        expedition.flush();
                }
            }
            if (pending_travel) {
                if (auto traveled = travel(*pending_travel); !traveled) {
                    log(traveled.error().message);
                    return 1;
                }
                pending_travel.reset();
            }
            // Bound per frame: traveling replaces them, but only between frames.
            Simulation &simulation = world->simulation;
            const Level &level = world->level;
            // Input and time are sampled once the display has caught up (docs/engine.md).
            if (auto paced = (*renderer)->wait_for_display(diagnostics.display_pacing); !paced) {
                log(paced.error().message);
                return 1;
            }
            // Replays ignore live look input, so they never grab the cursor.
            const bool capture_mouse = !tuning_open && !menu.is_open() && !options.camera_test &&
                                       !replaying && !options.progress_test;
            // While tuning is open a controller drives the menu, so its gameplay input is
            // withheld; the keyboard keeps moving the player unless ImGui is taking text.
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); },
                {.capture_mouse = capture_mouse && !(*renderer)->ui_wants_mouse(),
                 .gamepad_gameplay = !tuning_open && !menu.is_open()});
            const bool gameplay_input = input.focused && !(*renderer)->ui_wants_keyboard();
            if (input.quit || choice.kind == MenuChoice::Kind::quit)
                break;
            if (interactive && input.toggle_menu) {
                if (menu.is_open())
                    menu.close();
                else
                    menu.open();
            }
            // Leaving the window pauses live play, so nothing happens while you're away.
            if (interactive && !replaying && was_focused && !input.focused && !menu.is_open())
                menu.open();
            if (choice.kind == MenuChoice::Kind::resume) {
                menu.close();
            } else if (choice.kind == MenuChoice::Kind::tuning) {
                menu.close();
                tuning_open = true;
            } else if (choice.kind == MenuChoice::Kind::travel && choice.place < places.size()) {
                menu.close();
                const auto &place = places[choice.place];
                if (auto loaded = load_level(place.path); !loaded) {
                    log("Cannot travel to " + place.title + ": " + loaded.error().message);
                } else if (auto traveled =
                               arrive(place.path, std::move(*loaded), std::nullopt, "by the menu");
                           !traveled) {
                    log(traveled.error().message);
                    return 1;
                }
            }
            choice = {};
            if (menu.is_open() != was_paused) {
                was_paused = menu.is_open();
                log(was_paused ? "Paused" : "Resumed");
            }
            if (applied_options != player_options || applied_to != &world->simulation) {
                // The camera belongs to the simulation, so a new world needs them again.
                const FollowCameraTuning defaults;
                auto &camera = world->simulation.camera.tuning;
                camera.mouse_sensitivity =
                    defaults.mouse_sensitivity * player_options.mouse_sensitivity;
                camera.stick_yaw_speed = defaults.stick_yaw_speed * player_options.stick_speed;
                camera.stick_pitch_speed = defaults.stick_pitch_speed * player_options.stick_speed;
                camera.invert_y = player_options.invert_y;
                applied_options = player_options;
                applied_to = &world->simulation;
            }
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
            if (options.progress_test) {
                simulation_elapsed =
                    options.progress_test == ProgressTest::collect ? FixedStep::k_tick : 0;
                live = {0, 1, 0, -0.32F, 0};
            } else if (use_free_camera && (gameplay_input || options.camera_test)) {
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
                if (input.dive_pressed)
                    pending_buttons |= input_button::dive_pressed;
            }
            if (menu.is_open()) { // Paused: the world waits, and so do presses made meanwhile.
                simulation_elapsed = 0;
                pending_buttons = 0;
            }
            std::uint32_t frame_ticks = 0;
            bool progress_changed = false;
            const auto simulation_started = std::chrono::steady_clock::now();
            float alpha = fixed.advance(simulation_elapsed, [&](float dt) {
                // A capture is a simulation snapshot: don't run past its requested tick,
                // even when a slow rendered frame advances several simulation ticks.
                if (options.capture_tick && simulation.ticks() >= *options.capture_tick)
                    return;
                if (pending_travel)
                    return; // The rest of this frame's time belongs to the next level.
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
                    if (!options.progress_test) {
                        tick.camera_yaw = simulation.camera.yaw();
                        tick.camera_pitch = simulation.camera.pitch();
                    }
                }
                if (!options.record.empty())
                    recording.push_back(tick);
                const auto found = simulation.collected_count();
                const auto checkpoint = simulation.active_checkpoint();
                const auto before = simulation.player.current();
                simulation.tick(tick, dt);
                sound.tick(before, simulation, dt, cues); // Listens only, like the character.
                progress_changed = progress_changed || found != simulation.collected_count() ||
                                   checkpoint != simulation.active_checkpoint();
                if (options.progress_test)
                    ++progress_test_ticks;
                character.tick(simulation.player.current(), dt); // Visual only, like the sky.
                if (simulation.collected_count() > found)
                    character.cheer();
                if (travel_allowed && simulation.portal_entered())
                    pending_travel = simulation.portal_entered();
                ++frame_ticks;
                lighting.cycle.advance(dt); // Visual only; never feeds back into gameplay.
            });
            if (progress_changed && expedition.remember(*world))
                expedition.flush();
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
                                 .grid = level.atmosphere.grid ? 1.0F : 0.0F,
                                 .receives_blob = true});
            // The grid stays off movers: world-space lines would swim across them.
            for (std::uint32_t i = 0; i < level.movers.size(); ++i) {
                const auto at = simulation.mover_pose(i, alpha);
                instances.push_back({.mesh = {k_first_mover_mesh + i},
                                     .model = yaw_translation(at.yaw, at.position),
                                     .receives_blob = true});
            }
            particles.clear();
            if (diagnostics.show_capsule) {
                instances.push_back(
                    {.mesh = k_player_mesh, .model = yaw_translation(pose.facing, pose.position)});
            } else {
                character_parts.clear();
                character.build(pose, alpha, character_parts);
                character.build_particles(alpha, particles);
                for (const auto &part : character_parts)
                    instances.push_back({.mesh = part.mesh == PartMesh::glossy ? k_glossy_part_mesh
                                                                               : k_matte_part_mesh,
                                         .model = part.model,
                                         .casts_shadow = part.casts_shadow,
                                         .tint = part.tint,
                                         .emissive = part.emissive});
            }
            for (std::size_t i = 0; i < level.collectibles.size(); ++i) {
                if (simulation.collectible_collected(i))
                    continue;
                const float phase = seconds * 2 + static_cast<float>(i) * 1.7F;
                const Vec3 position =
                    level.collectibles[i].position + Vec3{0, 0.1F * std::sin(phase), 0};
                instances.push_back({.mesh = k_spark_mesh,
                                     .model = yaw_translation(phase * 0.7F, position),
                                     .casts_shadow = false,
                                     .emissive = 1.2F});
            }
            if (const auto last_spark = simulation.last_collected()) {
                const float age = seconds - static_cast<float>(simulation.last_collection_tick()) *
                                                static_cast<float>(FixedStep::k_tick);
                if (age > 0 && age < 0.65F) {
                    const float life = age / 0.65F;
                    const float fade_in = std::min(age / 0.035F, 1.0F);
                    for (int mote = 0; mote < 6; ++mote) {
                        const float angle = static_cast<float>(mote) * k_tau / 6;
                        const Vec3 drift{std::cos(angle) * age * 2, age * 1.8F,
                                         std::sin(angle) * age * 2};
                        particles.push_back(
                            {.position = level.collectibles[*last_spark].position + drift,
                             .radius = 0.16F + 0.08F * life,
                             .color = {1, 0.65F, 0.15F},
                             .opacity = fade_in * (1 - life) * 0.9F,
                             .emissive = (1 - life) * 2});
                    }
                }
            }
            for (const auto &portal : level.portals) {
                const auto look = portal_look(portal, seconds);
                instances.push_back({.mesh = k_portal_ring_mesh,
                                     .model = look.ring,
                                     .casts_shadow = false,
                                     .tint = {1, 0.78F, 0.4F},
                                     .emissive = 1.1F * look.glow});
                instances.push_back({.mesh = k_glossy_part_mesh,
                                     .model = look.veil,
                                     .casts_shadow = false,
                                     .tint = {0.32F, 0.55F, 1},
                                     .emissive = 0.7F + 0.5F * look.glow});
                portal_motes(portal, seconds, particles);
            }
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
            TravelNote travel_note;
            if (arrived) {
                travel_note.age = std::chrono::duration<float>(now - *arrived).count();
                if (travel_note.age < 3.5F) {
                    travel_note = {level.title, std::string(zone_at(level, pose.position)),
                                   travel_note.age, true};
                }
            }
            for (const auto &portal : level.portals) {
                const Vec3 middle = pose.position + Vec3{0, 0.8F, 0};
                if (!travel_note.title.empty() ||
                    length(middle - portal.position) > portal.radius + 3.5F)
                    continue;
                travel_note.title = portal.name;
                travel_note.subtitle = travel_allowed ? "Step into the light to travel"
                                                      : "Gates rest while recording or replaying";
            }
            const std::function<void()> debug_windows = [&] {
                if (!options.hide_overlay && !tuning_open) {
                    collectible_hud(simulation, level);
                    travel_hud(travel_note);
                    if (expedition.controls.error ||
                        now - expedition.noted < std::chrono::seconds(3))
                        progress_hud(expedition.controls);
                }
                tuning_window(simulation,
                              {lighting, (*window)->gamepad_tuning, diagnostics,
                               &expedition.controls, &audio_controls},
                              tuning_open);
                if (menu.is_open()) {
                    place_status.assign(places.size(), {});
                    for (std::size_t i = 0; i < places.size(); ++i) {
                        auto &place = place_status[i];
                        place.here = places[i].name == level.name;
                        for (const auto &saved : expedition.saved.levels) {
                            if (saved.name == places[i].name) {
                                place.visited = true;
                                place.found = saved.sparks.size();
                            }
                        }
                        place.visited = place.visited || place.here;
                        if (place.here)
                            place.found = simulation.collected_count();
                    }
                    choice = menu.draw({places, place_status, input.gamepad_labels,
                                        input.gamepad_connected, travel_allowed,
                                        expedition.controls.enabled && expedition.writable},
                                       player_options, audio_controls);
                }
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
                                          : simulation.camera.pose(alpha, simulation.scene());
            if (mixer) { // Heard from the camera; far-off machines aren't worth a voice.
                const Vec3 ear = view.camera.position;
                mixer->set_listener({ear, yaw_right(view.camera.yaw)});
                mixer->set_volume(audio_controls.muted
                                      ? 0.0F
                                      : audio_controls.volume * (menu.is_open() ? 0.35F : 1.0F));
                for (const auto &cue : cues) {
                    if (!cue.position || length(*cue.position - ear) < cue.reach)
                        mixer->play({static_cast<std::uint32_t>(cue.cue), cue.gain, cue.pitch,
                                     cue.position, cue.reach});
                }
                for (const auto &loop : sound.loops())
                    mixer->set_loop({static_cast<std::uint32_t>(loop.cue), loop.gain, loop.pitch,
                                     loop.position, loop.reach});
            }
            cues.clear();
            view.instances = instances;
            view.particles = particles;
            view.keyboard_navigation = menu.is_open();
            view.blob = {pose.position,
                         lighting.blob ? simulation.player.tuning.radius * 1.35F : 0.0F,
                         lighting.blob_strength};
            view.environment = environment_at(lighting.cycle);
            view.environment.fog_density *= level.atmosphere.fog;
            if (level.atmosphere.sky_below) // The haze continues down, and so does its light.
                view.environment.ground = view.environment.sky_horizon;
            if (arrived) { // A bright breath of light as you step out of a gate.
                const float since = std::chrono::duration<float>(now - *arrived).count();
                const float flash = std::max(0.0F, 1 - since / 0.9F);
                view.environment.exposure *= 1 + 6 * flash * flash;
            }
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
            // --travel-test: out through the first gate, then back through the far side's.
            if (options.travel_test && (rendered == 4 || rendered == 10) && !level.portals.empty())
                pending_travel = 0;
            if (options.frames && rendered >= *options.frames)
                break;
            if (replay_finished)
                break;
            if (options.progress_test &&
                (options.progress_test != ProgressTest::collect || progress_test_ticks >= 240))
                break;
        }
        if (expedition.remember(*world))
            expedition.flush();
        expedition.describe("end", *world);
        if (options.frames || options.capture_frame || options.capture_tick)
            log("Rendered frames: " + std::to_string(rendered));
        if ((options.capture_frame || options.capture_tick) && !captured) {
            log("Window closed before the requested capture");
            return 1;
        }
    }
    if (replaying)
        log("Replay " + std::string(replay_index >= replay.size() ? "finished" : "stopped") + ": " +
            describe(world->simulation));
    if (!options.record.empty()) {
        if (auto saved = save_replay(options.record, recording, world->level.name); !saved) {
            log(saved.error().message);
            return 1;
        }
        log("Recorded " + std::to_string(recording.size()) + " ticks to " +
            options.record.string() + ": " + describe(world->simulation));
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
            "  --save PATH  Use a separate expedition profile (also enables custom-level saves).\n"
            "  --no-save  Keep this expedition in memory only.\n"
            "  --no-audio  Play without sound.\n"
            "  --show-menu PAGE  Open the pause menu (main, places, options, controls, quit).\n"
            "  --time-of-day H  Start at H hours (0-24; 12 noon). Default 14.5.\n"
            "  --shadow-quality low|medium|high  Sun shadow preset. Default medium.\n"
            "  --hide-overlay  Hide diagnostics and the spark HUD (clean captures).\n"
            "  --capture-tick N  Capture the exact state at simulation tick N.\n"
            "  --frame-log PATH  Write per-frame timing (CSV) for frame-pacing diagnostics.\n"
            "  --resize-test / --camera-test / --travel-test  Scripted render correctness checks.\n"
            "  Debug builds require Vulkan validation layers.\n"
            "  WASD move, Space jump/wall kick, Shift+jump long jump, E or left click dive, Ctrl "
            "walk, mouse look,\n"
            "  R respawn, F1 overlay, F2 tuning, F3 free camera, Escape pause. Gamepad: sticks, "
            "South jump, West dive,\n"
            "  LB+jump long jump, R3 center, Back respawn, Start pause.");
        return 0;
    }
    return mj::game::run(*options);
}
