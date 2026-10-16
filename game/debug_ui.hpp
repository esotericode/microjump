#pragma once

#include "lighting.hpp"
#include "simulation.hpp"

#include <string>

namespace mj::game {

struct SessionStatus {
    bool free_camera = false;
    bool gamepad = false;
    std::string input_mode; // "Live", "Recording: path" or "Replay: n/m".
    float hours = 12;
    bool day_animating = false;
};

// Diagnostics overlay lines: movement state, speeds, checkpoint and controls.
void movement_overlay(const Simulation &simulation, const SessionStatus &status);
// Live tuning window (F2). Movement edits apply from the next tick, lighting immediately.
void tuning_window(Simulation &simulation, LightingControls &lighting, bool &open);

} // namespace mj::game
