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
    bool show_capsule = false; // Draw the collision capsule instead of the character.
};

// Diagnostics overlay lines: movement state, speeds, checkpoint and controls.
void movement_overlay(const Simulation &simulation, const SessionStatus &status);
// Gameplay progress stays visible when F1 diagnostics are closed.
void collectible_hud(const Simulation &simulation, const Level &level);
// What the gates say: the place you just arrived in, or the gate you are standing near.
struct TravelNote {
    std::string title;    // Arrival: the level name. Near a gate: its name.
    std::string subtitle; // e.g. "Step through to travel" or why a gate is closed.
    float age = 0;        // Seconds since it appeared; arrival cards fade after a few.
    bool arrival = false;
};
void travel_hud(const TravelNote &note);
struct ProgressControls {
    bool enabled = false;
    bool error = false;
    bool reset_requested = false;
    bool confirm_reset = false;
    std::string message;
};
// Sound settings for the F2 panel. Without a device, the panel says so.
struct AudioControls {
    bool available = false;
    std::string driver;
    float volume = 0.8F;
    bool muted = false;
};
// Brief save/resume feedback; main decides when the message expires.
void progress_hud(const ProgressControls &progress);
// Live tuning window (F2). Movement edits apply from the next tick, lighting immediately.
struct TuningPanels {
    LightingControls &lighting;
    GamepadTuning &gamepad;
    DiagnosticsControls &diagnostics;
    ProgressControls *progress = nullptr;
    AudioControls *audio = nullptr;
};
void tuning_window(Simulation &simulation, const TuningPanels &panels, bool &open);

} // namespace mj::game
