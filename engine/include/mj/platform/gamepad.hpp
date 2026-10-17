#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct SDL_Gamepad;
union SDL_Event;

namespace mj {
struct StickReading {
    float x = 0;
    float y = 0;
};
// Radial dead zone, rescaled to full tilt. Invalid tuning safely uses the default.
[[nodiscard]] StickReading apply_dead_zone(StickReading raw, float dead_zone);
// Button names for on-screen prompts, by controller family. `jump` comes from SDL's
// face-button label (A, B or Cross) when known.
enum class GamepadFamily : std::uint8_t { generic, xbox, playstation, nintendo };
struct GamepadLabels {
    std::string_view jump = "South";
    std::string_view long_jump = "Left shoulder";
    std::string_view dive = "West";
    std::string_view walk = "Right shoulder";
    std::string_view recenter = "Right stick";
    std::string_view respawn = "Back";
    std::string_view menu = "Start";
};
[[nodiscard]] GamepadLabels gamepad_labels(GamepadFamily family, std::string_view jump);

struct GamepadTuning {
    float move_dead_zone = 0.18F;
    float look_dead_zone = 0.18F;
};
struct GamepadFrame {
    StickReading move;
    StickReading look; // SDL convention: +Y down.
    bool connected = false;
    bool jump_held = false;
    bool jump_pressed = false;
    bool long_jump_held = false; // Includes the modifier at a short jump press.
    bool dive_pressed = false;
    bool walk_held = false;
    bool reset_pressed = false;
    bool recenter_pressed = false;
    bool toggle_menu = false; // Start: pause.
    bool toggle_overlay = false;
    std::string name;
    GamepadLabels labels;
};
// One active SDL gamepad. SDL must outlive this owner; no video/window is required.
// Existing devices work at startup; disconnect hands off to another connected pad.
class Gamepad {
  public:
    Gamepad() = default;
    ~Gamepad();
    Gamepad(const Gamepad &) = delete;
    Gamepad &operator=(const Gamepad &) = delete;
    void open_available();
    void close();
    void process_event(const SDL_Event &event);
    // Consumes press edges even when unfocused, so background taps cannot replay later.
    [[nodiscard]] GamepadFrame sample(const GamepadTuning &tuning, bool focused);

  private:
    SDL_Gamepad *m_handle = nullptr;
    GamepadFrame m_pending;
    bool m_long_jump_down = false;
};
} // namespace mj
