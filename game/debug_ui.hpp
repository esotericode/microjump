#pragma once

#include "frame_pacing.hpp"
#include "lighting.hpp"
#include "simulation.hpp"

#include <mj/platform/gamepad.hpp>
#include <mj/render/pacing.hpp>

#include <string>

namespace mj::game {

struct SessionStatus {
    bool free_camera = false;
    std::string gamepad_name;
    GamepadLabels gamepad_labels;
    std::string input_mode; // "Live", "Recording: path" or "Replay: n/m".
    std::string level;
    std::string_view zone;
    float hours = 12;
    bool day_animating = false;
    const FramePacing *pacing = nullptr; // Frame-time graph and jitter statistics.
};

// F2 > Diagnostics: frame-pacing experiments. Buttons set one-shot requests for main.
struct DiagnosticsControls {
    bool snap_to_refresh = false;
    bool display_pacing_supported = false; // VK_KHR_present_wait is available.
    DisplayPacing display_pacing;
    float refresh_hz = 0;
    bool logging = false;
    std::string log_path;
    bool toggle_log = false;
    bool wait_idle = false;
    bool rebuild_shadow_map = false;
    bool rebuild_swapchain = false;
};

// Diagnostics overlay lines: movement state, speeds, checkpoint and controls.
void movement_overlay(const Simulation &simulation, const SessionStatus &status);
// Live tuning window (F2). Movement edits apply from the next tick, lighting immediately.
struct TuningPanels {
    LightingControls &lighting;
    GamepadTuning &gamepad;
    DiagnosticsControls &diagnostics;
};
void tuning_window(Simulation &simulation, const TuningPanels &panels, bool &open);

} // namespace mj::game
