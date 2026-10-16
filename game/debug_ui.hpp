#pragma once

#include "lighting.hpp"
#include "simulation.hpp"

#include <mj/platform/gamepad.hpp>

#include <string>

namespace mj::game {

struct SessionStatus {
    bool free_camera = false;
    std::string gamepad_name;
    std::string_view jump_label = "South";
    std::string input_mode; // "Live", "Recording: path" or "Replay: n/m".
    std::string level;
    std::string_view zone;
    float hours = 12;
    bool day_animating = false;
};

// Diagnostics overlay lines: movement state, speeds, checkpoint and controls.
void movement_overlay(const Simulation &simulation, const SessionStatus &status);
// Live tuning window (F2). Movement edits apply from the next tick, lighting immediately.
void tuning_window(Simulation &simulation, LightingControls &lighting, GamepadTuning &gamepad,
                   bool &open);

} // namespace mj::game
