#pragma once

#include "test_framework.hpp"
#include <SDL3/SDL.h>

namespace mj::test {
inline void virtual_gamepads_only() {
    // Test-process hints only; never modify the owner's SDL/runtime configuration.
    for (const char *hint :
         {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_LINUX_CLASSIC, SDL_HINT_JOYSTICK_RAWINPUT,
          SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT,
          SDL_HINT_XINPUT_ENABLED})
        SDL_SetHint(hint, "0");
}
struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Joystick *handle = nullptr;
    explicit VirtualPad(const char *name = "microjump virtual controller") {
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = static_cast<Uint16>(SDL_JOYSTICK_TYPE_GAMEPAD);
        desc.naxes = static_cast<Uint16>(SDL_GAMEPAD_AXIS_COUNT);
        desc.nbuttons = static_cast<Uint16>(SDL_GAMEPAD_BUTTON_COUNT);
        desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_COUNT) - 1;
        desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.name = name;
        id = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(id != 0);
        handle = SDL_OpenJoystick(id);
        REQUIRE(handle != nullptr);
        REQUIRE(SDL_IsGamepad(id));
    }
    ~VirtualPad() {
        detach();
    }
    void detach() {
        if (handle != nullptr) {
            SDL_CloseJoystick(handle);
            handle = nullptr;
        }
        if (id != 0) {
            CHECK(SDL_DetachVirtualJoystick(id));
            id = 0;
        }
    }
    void axis(SDL_GamepadAxis which, Sint16 value) {
        REQUIRE(SDL_SetJoystickVirtualAxis(handle, static_cast<int>(which), value));
    }
    void button(SDL_GamepadButton which, bool down) {
        REQUIRE(SDL_SetJoystickVirtualButton(handle, static_cast<int>(which), down));
    }
};
} // namespace mj::test
