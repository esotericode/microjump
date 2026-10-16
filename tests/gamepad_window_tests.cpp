#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "test_framework.hpp"
#include "virtual_gamepad.hpp"

#include <debug_ui.hpp>

#include <mj/core/fixed_step.hpp>
#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <array>

TEST_CASE("virtual gamepad drives the real window, gameplay, camera and tuning menu") {
    mj::test::virtual_gamepads_only();
    mj::ValidationLog validation;
    {
        auto window = mj::Window::create();
        REQUIRE(window);
        auto context = mj::VulkanContext::create(**window, validation);
        REQUIRE(context);
        auto level = mj::game::make_movement_yard();
        level.checkpoints[0] = {{7, 0.01F, -25}, 0};
        const auto geometry = mj::game::build_geometry(level);
        mj::game::Simulation simulation(level, geometry.collision);
        const std::array meshes{geometry.mesh, mj::game::make_player_mesh(0.35F, 1.6F)};
        auto renderer = mj::Renderer::create(**window, **context, meshes);
        REQUIRE(renderer);
        mj::test::VirtualPad pad("microjump controller test");
        REQUIRE(SDL_RaiseWindow((*window)->native_handle()));
        REQUIRE(SDL_SyncWindow((*window)->native_handle()));
        // Wayland maps a Vulkan window only after its first presentation. Process focus
        // normally, rather than synthesizing focus or accepting background gamepad input.
        bool ready = false;
        for (int attempt = 0; attempt < 60 && !ready; ++attempt) {
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); }, {});
            mj::FrameView warmup;
            warmup.overlay = false;
            auto drawn = (*renderer)->draw(warmup);
            REQUIRE(drawn);
            ready = input.focused;
            SDL_Delay(17);
        }
        REQUIRE(ready);
        mj::game::LightingControls lighting;
        mj::game::SessionStatus status;
        status.level = level.name;
        status.input_mode = "SDL virtual gamepad test";
        bool open = false, jumped = false, long_jumped = false, centered = false, menu_nav = false;
        float menu_x = 0, menu_z = 0;
        const float initial_hours = lighting.cycle.hours;
        std::uint32_t pending = 0;
        mj::FixedStep fixed;
        std::vector<mj::TickInput> recorded;
        for (int frame = 0; frame < 160; ++frame) {
            auto button = [&](SDL_GamepadButton which, bool down) { pad.button(which, down); };
            if (frame == 4) {
                pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -32768);
                pad.axis(SDL_GAMEPAD_AXIS_RIGHTX, 9000);
            }
            if (frame == 30) {
                button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
                button(SDL_GAMEPAD_BUTTON_SOUTH, true);
            }
            if (frame == 48) {
                button(SDL_GAMEPAD_BUTTON_SOUTH, false);
                button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
                pad.axis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
            }
            if (frame == 65)
                button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
            if (frame == 66)
                button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
            if (frame == 75) {
                pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
                button(SDL_GAMEPAD_BUTTON_BACK, true);
            }
            if (frame == 76)
                button(SDL_GAMEPAD_BUTTON_BACK, false);
            if (frame == 90 || frame == 145)
                button(SDL_GAMEPAD_BUTTON_START, true);
            if (frame == 91 || frame == 146)
                button(SDL_GAMEPAD_BUTTON_START, false);
            if (frame == 95)
                button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
            if (frame == 96)
                button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
            if (frame == 98 || frame == 134)
                button(SDL_GAMEPAD_BUTTON_SOUTH, true); // Select the focused slider, then commit.
            if (frame == 99 || frame == 135)
                button(SDL_GAMEPAD_BUTTON_SOUTH, false);
            if (frame == 100)
                button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
            if (frame == 130)
                button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
            if (frame == 148)
                button(SDL_GAMEPAD_BUTTON_SOUTH, true);
            if (frame == 155)
                button(SDL_GAMEPAD_BUTTON_SOUTH, false);

            // As in the game: the window withholds gamepad gameplay while the menu is open.
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); },
                {.gamepad_gameplay = !open});
            REQUIRE(input.gamepad_connected);
            REQUIRE(input.focused);
            if (input.toggle_tuning)
                open = !open;
            mj::TickInput live;
            {
                if (input.recenter_pressed) {
                    simulation.camera.set_orientation({simulation.player.current().facing,
                                                       simulation.camera.tuning.default_pitch});
                    CHECK(simulation.camera.yaw() == simulation.player.current().facing);
                    CHECK(simulation.camera.pitch() == simulation.camera.tuning.default_pitch);
                    centered = true;
                }
                simulation.camera.look({input.look_x, input.look_y, input.look_stick_x,
                                        input.look_stick_y, 1.0F / 60});
                live.move_x = input.move_x;
                live.move_y = input.move_y;
                if (input.jump_held)
                    live.buttons |= mj::input_button::jump_held;
                if (input.long_jump_held)
                    live.buttons |= mj::input_button::long_jump_held;
                if (input.jump_pressed)
                    pending |= mj::input_button::jump_pressed;
                if (input.reset_pressed)
                    pending |= mj::input_button::reset_pressed;
            }
            fixed.advance(1.0 / 60, [&](float dt) {
                auto tick = live;
                tick.buttons |= pending;
                pending = 0;
                tick.camera_yaw = simulation.camera.yaw();
                tick.camera_pitch = simulation.camera.pitch();
                simulation.tick(tick, dt);
                recorded.push_back(tick);
                long_jumped = long_jumped ||
                              simulation.player.current().state == mj::game::MoveState::long_jump;
                jumped = jumped || simulation.player.current().state == mj::game::MoveState::jump;
            });
            if (frame == 90) {
                menu_x = simulation.player.current().position.x;
                menu_z = simulation.player.current().position.z;
            }
            if (frame == 140) {
                CHECK(open);
                CHECK(simulation.player.current().position.x == menu_x);
                CHECK(simulation.player.current().position.z == menu_z);
            }
            status.hours = lighting.cycle.hours;
            status.zone = mj::game::zone_at(level, simulation.player.current().position);
            status.gamepad_name = input.gamepad_name;
            status.gamepad_labels = input.gamepad_labels;
            const auto pose = simulation.player.interpolated(1);
            const std::array instances{
                mj::DrawInstance{.mesh = {0},
                                 .model = mj::yaw_translation(0, {}),
                                 .grid = 0.3F,
                                 .receives_blob = true},
                mj::DrawInstance{.mesh = {1},
                                 .model = mj::yaw_translation(pose.facing, pose.position)}};
            const std::function<void()> overlay = [&] {
                mj::game::movement_overlay(simulation, status);
            };
            const std::function<void()> ui = [&] {
                mj::game::tuning_window(simulation, lighting, (*window)->gamepad_tuning, open);
            };
            const auto capture =
                mj::Window::executable_directory() / "../tests/captures/gamepad.png";
            mj::FrameView view;
            view.camera = simulation.camera.pose(1, geometry.collision);
            view.instances = instances;
            view.environment = mj::game::environment_at(lighting.cycle);
            view.shadows = lighting.shadows;
            view.elapsed = 1.0 / 60;
            view.blob = {pose.position, 0.35F};
            view.overlay_lines = &overlay;
            view.debug_windows = &ui;
            if (frame == 140)
                view.capture = &capture;
            auto drawn = (*renderer)->draw(view);
            INFO((drawn ? "drawn" : drawn.error().message));
            REQUIRE(drawn);
            REQUIRE(*drawn);
            if (open)
                menu_nav = menu_nav || ImGui::GetIO().NavActive;
            // Give SDL/ImGui a real input interval; correctness is checked, not frame throughput.
            SDL_Delay(17);
        }
        CHECK(centered);
        CHECK(long_jumped);
        CHECK(jumped);
        CHECK(simulation.respawns() == 1);
        CHECK(menu_nav);
        CHECK(lighting.cycle.hours > initial_hours);
        const auto replay = mj::parse_replay(mj::serialize_replay(recorded));
        REQUIRE(replay);
        mj::game::Simulation replayed(level, geometry.collision);
        for (const auto &tick : *replay)
            replayed.tick(tick, 1.0F / 120);
        const auto actual = simulation.player.current().position;
        const auto repeated = replayed.player.current().position;
        CHECK(actual.x == repeated.x);
        CHECK(actual.y == repeated.y);
        CHECK(actual.z == repeated.z);
    }
    CHECK(validation.error_count == 0);
}
