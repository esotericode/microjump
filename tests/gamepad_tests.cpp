#include "test_framework.hpp"
#include "virtual_gamepad.hpp"

#include <mj/platform/gamepad.hpp>

#include <SDL3/SDL.h>

#include <cmath>
#include <limits>

namespace {
struct SdlSession {
    SdlSession() {
        mj::test::virtual_gamepads_only();
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        REQUIRE(SDL_Init(SDL_INIT_GAMEPAD));
    }
    ~SdlSession() {
        SDL_Quit();
    }
};
using mj::test::VirtualPad;
struct Rig {
    SdlSession sdl;
    VirtualPad pad;
    mj::Gamepad gamepad;
    mj::GamepadTuning tuning;
    Rig() {
        gamepad.open_available();
        pump();
    }
    void pump() {
        SDL_UpdateJoysticks();
        SDL_Event event;
        while (SDL_PollEvent(&event))
            gamepad.process_event(event);
    }
    mj::GamepadFrame sample(bool focused = true) {
        pump();
        return gamepad.sample(tuning, focused);
    }
};
} // namespace

TEST_CASE("SDL gamepad opens an already connected controller and reads analog sticks") {
    Rig rig;
    auto state = rig.sample();
    REQUIRE(state.connected);
    CHECK(state.name == "microjump virtual controller");
    rig.pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 32767);
    rig.pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -32768);
    rig.pad.axis(SDL_GAMEPAD_AXIS_RIGHTX, 16384);
    rig.pad.axis(SDL_GAMEPAD_AXIS_RIGHTY, -32768);
    state = rig.sample();
    CHECK(state.move.x > 0.7F);
    CHECK(state.move.y > 0.7F);
    CHECK(state.move.x * state.move.x + state.move.y * state.move.y == doctest::Approx(1.0F));
    CHECK(state.look.x > 0.0F);
    CHECK(state.look.y < 0.0F);
    rig.pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 16000);
    rig.pad.axis(SDL_GAMEPAD_AXIS_LEFTY, 0);
    rig.pad.axis(SDL_GAMEPAD_AXIS_RIGHTX, 3000);
    rig.pad.axis(SDL_GAMEPAD_AXIS_RIGHTY, 0);
    state = rig.sample();
    CHECK(state.move.x > 0.3F);
    CHECK(state.move.x < 0.5F);
    CHECK(state.look.x == 0.0F);
    rig.tuning.move_dead_zone = 0.5F;
    CHECK(rig.sample().move.x == 0.0F);
}

TEST_CASE("SDL gamepad preserves a full shoulder+jump tap between polls") {
    Rig rig;
    rig.pad.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
    rig.pump();
    rig.pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    rig.pump();
    rig.pad.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    rig.pad.button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
    auto state = rig.sample();
    CHECK(state.jump_pressed);
    CHECK_FALSE(state.jump_held);
    CHECK(state.long_jump_held);
    state = rig.sample();
    CHECK_FALSE(state.jump_pressed);
    CHECK_FALSE(state.long_jump_held);
    rig.pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    state = rig.sample();
    CHECK(state.jump_pressed);
    CHECK(state.jump_held);
    CHECK_FALSE(state.long_jump_held);
    state = rig.sample();
    CHECK_FALSE(state.jump_pressed);
    CHECK(state.jump_held);
}

TEST_CASE("SDL gamepad supports walk, recenter, respawn, tuning and overlay") {
    Rig rig;
    rig.pad.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    rig.pad.button(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    rig.pad.button(SDL_GAMEPAD_BUTTON_BACK, true);
    rig.pad.button(SDL_GAMEPAD_BUTTON_START, true);
    rig.pad.button(SDL_GAMEPAD_BUTTON_NORTH, true);
    const auto pressed = rig.sample();
    CHECK(pressed.walk_held);
    CHECK(pressed.recenter_pressed);
    CHECK(pressed.reset_pressed);
    CHECK(pressed.toggle_tuning);
    CHECK(pressed.toggle_overlay);
    const auto held = rig.sample();
    CHECK(held.walk_held);
    CHECK_FALSE(held.recenter_pressed);
    CHECK_FALSE(held.reset_pressed);
    CHECK_FALSE(held.toggle_tuning);
    CHECK_FALSE(held.toggle_overlay);
}

TEST_CASE("SDL gamepad drops background input and disconnect never leaves stuck controls") {
    Rig rig;
    rig.pad.axis(SDL_GAMEPAD_AXIS_LEFTY, -32768);
    rig.pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    rig.pad.button(SDL_GAMEPAD_BUTTON_START, true);
    auto state = rig.sample(false);
    CHECK(state.connected);
    CHECK(state.move.y == 0.0F);
    CHECK_FALSE(state.jump_pressed);
    CHECK_FALSE(state.jump_held);
    CHECK_FALSE(state.toggle_tuning);
    rig.pad.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    rig.pad.button(SDL_GAMEPAD_BUTTON_START, false);
    state = rig.sample();
    CHECK_FALSE(state.jump_pressed);
    CHECK_FALSE(state.toggle_tuning);
    rig.pad.detach();
    state = rig.sample();
    CHECK_FALSE(state.connected);
    CHECK(state.move.y == 0.0F);
    CHECK_FALSE(state.jump_held);
    CHECK(state.name.empty());
    VirtualPad reconnected("Reconnected pad");
    state = rig.sample();
    CHECK(state.connected);
    CHECK(state.name == "Reconnected pad");
    CHECK_FALSE(state.jump_pressed);
}

TEST_CASE("SDL gamepad switches to an already connected backup when the active pad leaves") {
    Rig rig;
    VirtualPad backup("Backup pad");
    backup.axis(SDL_GAMEPAD_AXIS_LEFTX, -32768);
    backup.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    auto state = rig.sample();
    CHECK(state.name == "microjump virtual controller");
    CHECK_FALSE(state.jump_pressed);
    CHECK(state.move.x == 0.0F);
    rig.pad.detach();
    // Sample before draining the queued removal event, as can happen at a poll boundary.
    state = rig.gamepad.sample(rig.tuning, true);
    REQUIRE(state.connected);
    CHECK(state.name == "Backup pad");
    state = rig.sample(); // The late removal must leave the new selection intact.
    REQUIRE(state.connected);
    CHECK(state.name == "Backup pad");
    CHECK(state.move.x == doctest::Approx(-1.0F));
    CHECK_FALSE(state.jump_pressed); // An old backup press is not a fresh jump.
}

TEST_CASE("radial stick tuning remains finite at invalid and extreme values") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float dead_zone : {-2.0F, 0.0F, 0.18F, 0.95F, 1.0F, 2.0F, nan}) {
        const auto neutral = mj::apply_dead_zone({}, dead_zone);
        CHECK(neutral.x == 0.0F);
        CHECK(neutral.y == 0.0F);
        const auto full = mj::apply_dead_zone({1, 1}, dead_zone);
        CHECK(std::isfinite(full.x));
        CHECK(full.x * full.x + full.y * full.y == doctest::Approx(1.0F));
    }
    CHECK(mj::apply_dead_zone({nan, 1}, 0.18F).y == 0.0F);
    CHECK(mj::apply_dead_zone({0.1F, 0.1F}, 0.18F).x == 0.0F);
}
