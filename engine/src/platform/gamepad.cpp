#include <mj/platform/gamepad.hpp>

#include <mj/core/error.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace mj {
namespace {
float axis(SDL_Gamepad *gamepad, SDL_GamepadAxis which) {
    return std::clamp(static_cast<float>(SDL_GetGamepadAxis(gamepad, which)) / 32767.0F, -1.0F,
                      1.0F);
}
GamepadFamily family(SDL_Gamepad *gamepad) {
    switch (SDL_GetGamepadType(gamepad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return GamepadFamily::xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return GamepadFamily::playstation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return GamepadFamily::nintendo;
    default:
        return GamepadFamily::generic;
    }
}
std::string_view jump_label(SDL_Gamepad *gamepad) {
    switch (SDL_GetGamepadButtonLabel(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
        return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
        return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        return "Cross";
    default:
        return "South";
    }
}
} // namespace

GamepadLabels gamepad_labels(GamepadFamily family, std::string_view jump) {
    GamepadLabels labels;
    labels.jump = jump;
    switch (family) {
    case GamepadFamily::xbox:
        return {jump, "LB", "X", "RB", "RS (click)", "View", "Menu"};
    case GamepadFamily::playstation:
        return {jump, "L1", "Square", "R1", "R3", "Share/Create", "Options"};
    case GamepadFamily::nintendo:
        return {jump, "L", "Y", "R", "R stick (click)", "-", "+"};
    case GamepadFamily::generic:
        break;
    }
    return labels;
}

StickReading apply_dead_zone(StickReading raw, float dead_zone) {
    if (!std::isfinite(raw.x) || !std::isfinite(raw.y))
        return {};
    if (!std::isfinite(dead_zone))
        dead_zone = 0.18F;
    dead_zone = std::clamp(dead_zone, 0.0F, 0.95F);
    raw.x = std::clamp(raw.x, -1.0F, 1.0F);
    raw.y = std::clamp(raw.y, -1.0F, 1.0F);
    const float magnitude = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (magnitude <= dead_zone || magnitude <= 0)
        return {};
    const float scaled = std::min((magnitude - dead_zone) / (1 - dead_zone), 1.0F);
    return {raw.x / magnitude * scaled, raw.y / magnitude * scaled};
}

Gamepad::~Gamepad() {
    close();
}
void Gamepad::close() {
    if (m_handle != nullptr)
        SDL_CloseGamepad(m_handle);
    m_handle = nullptr;
    m_pending = {};
    m_long_jump_down = false;
}
void Gamepad::open_available() {
    if (m_handle != nullptr)
        return;
    int count = 0;
    SDL_JoystickID *devices = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) {
        m_handle = SDL_OpenGamepad(devices[i]);
        if (m_handle != nullptr) {
            m_long_jump_down = SDL_GetGamepadButton(m_handle, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            const char *name = SDL_GetGamepadName(m_handle);
            log(std::string("Gamepad connected: ") + (name != nullptr ? name : "unnamed"));
            break;
        }
        log(std::string("Gamepad open failed: ") + SDL_GetError());
    }
    SDL_free(devices);
}
void Gamepad::process_event(const SDL_Event &event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        open_available();
        return;
    }
    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (m_handle != nullptr && event.gdevice.which == SDL_GetGamepadID(m_handle)) {
            close();
            log("Gamepad disconnected");
        }
        open_available();
        return;
    }
    if (m_handle == nullptr)
        return;
    if ((event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
         event.type != SDL_EVENT_GAMEPAD_BUTTON_UP) ||
        event.gbutton.which != SDL_GetGamepadID(m_handle))
        return;
    const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
    const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
        m_long_jump_down = down;
    if (!down)
        return;
    if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
        m_pending.jump_pressed = true;
        m_pending.long_jump_held = m_pending.long_jump_held || m_long_jump_down;
    }
    m_pending.dive_pressed = m_pending.dive_pressed || button == SDL_GAMEPAD_BUTTON_WEST;
    m_pending.reset_pressed = m_pending.reset_pressed || button == SDL_GAMEPAD_BUTTON_BACK;
    m_pending.recenter_pressed =
        m_pending.recenter_pressed || button == SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    m_pending.toggle_menu = m_pending.toggle_menu || button == SDL_GAMEPAD_BUTTON_START;
    m_pending.toggle_overlay = m_pending.toggle_overlay || button == SDL_GAMEPAD_BUTTON_NORTH;
}
GamepadFrame Gamepad::sample(const GamepadTuning &tuning, bool focused) {
    GamepadFrame state = std::exchange(m_pending, {});
    if (m_handle != nullptr && !SDL_GamepadConnected(m_handle)) {
        // A removal can arrive between the last event poll and this sample. Never
        // retain the old edges or miss a backup just because its add event is past.
        close();
        open_available();
        state = {};
    }
    if (m_handle == nullptr)
        return {};
    // Even a lost-focus frame reports the connection; all controls remain neutral.
    if (!focused)
        state = {};
    state.connected = true;
    const char *name = SDL_GetGamepadName(m_handle);
    state.name = name != nullptr ? name : "Unnamed gamepad";
    state.labels = gamepad_labels(family(m_handle), jump_label(m_handle));
    if (!focused)
        return state;
    state.move = apply_dead_zone(
        {axis(m_handle, SDL_GAMEPAD_AXIS_LEFTX), -axis(m_handle, SDL_GAMEPAD_AXIS_LEFTY)},
        tuning.move_dead_zone);
    state.look = apply_dead_zone(
        {axis(m_handle, SDL_GAMEPAD_AXIS_RIGHTX), axis(m_handle, SDL_GAMEPAD_AXIS_RIGHTY)},
        tuning.look_dead_zone);
    state.jump_held = SDL_GetGamepadButton(m_handle, SDL_GAMEPAD_BUTTON_SOUTH);
    state.long_jump_held =
        state.long_jump_held || SDL_GetGamepadButton(m_handle, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    state.walk_held = SDL_GetGamepadButton(m_handle, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    return state;
}
} // namespace mj
