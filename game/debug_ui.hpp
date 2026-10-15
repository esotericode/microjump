#pragma once

#include "simulation.hpp"

#include <string>

namespace mj::game {

struct SessionStatus {
    bool free_camera = false;
    bool gamepad = false;
    std::string input_mode; // "Live", "Recording: path" or "Replay: n/m".
};

// Diagnostics overlay lines: movement state, speeds, checkpoint and controls.
void movement_overlay(const Simulation &simulation, const SessionStatus &status);
// Live tuning window (F2). Edits apply from the next tick.
void tuning_window(Simulation &simulation, bool &open);

} // namespace mj::game
