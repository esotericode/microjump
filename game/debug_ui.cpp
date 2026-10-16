#include "debug_ui.hpp"

#include <imgui.h>

namespace mj::game {

void collectible_hud(const Simulation &simulation, const Level &level) {
    if (simulation.collectible_count() == 0)
        return;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;
    const auto display = ImGui::GetIO().DisplaySize;
    const bool complete = simulation.collected_count() == simulation.collectible_count();
    const ImVec4 gold{1, 0.79F, 0.3F, 1};
    ImGui::SetNextWindowPos(ImVec2(display.x - 18, 18), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.88F);
    ImGui::Begin("Sparks", nullptr, flags);
    ImGui::TextColored(gold, "SPARKS  %zu / %zu", simulation.collected_count(),
                       simulation.collectible_count());
    ImGui::TextUnformatted(complete ? "Every spark found!" : "Explore. Climb. Follow the gold.");
    ImGui::End();

    const auto last = simulation.last_collected();
    const float age =
        static_cast<float>(simulation.ticks() - simulation.last_collection_tick()) / 120.0F;
    if (!last || age >= (complete ? 5.0F : 2.5F))
        return;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5F, display.y - 64), ImGuiCond_Always,
                            ImVec2(0.5F, 1));
    ImGui::SetNextWindowBgAlpha(0.9F);
    ImGui::Begin("Spark found", nullptr, flags);
    ImGui::TextColored(gold, "%s", complete ? "ALL SPARKS FOUND!" : "SPARK FOUND");
    ImGui::TextUnformatted(level.collectibles[*last].name.c_str());
    ImGui::End();
}

void movement_overlay(const Simulation &simulation, const SessionStatus &status) {
    const auto &s = simulation.player.current();
    const float speed = length(horizontal(s.velocity));
    ImGui::Text("Level: %s  |  %.*s", status.level.c_str(), static_cast<int>(status.zone.size()),
                status.zone.data());
    ImGui::Text("State: %-5s  %s", to_string(s.state), s.grounded ? "grounded" : "airborne");
    ImGui::Text("Speed %.2f m/s   Vy %+.2f m/s", static_cast<double>(speed),
                static_cast<double>(s.velocity.y));
    ImGui::Text("Feet %.2f %.2f %.2f", static_cast<double>(s.position.x),
                static_cast<double>(s.position.y), static_cast<double>(s.position.z));
    const float slope = std::acos(std::clamp(s.ground_normal.y, -1.0F, 1.0F)) * 180 / k_pi;
    ImGui::Text("Ground %.0f deg   Checkpoint %zu   Respawns %u", static_cast<double>(slope),
                simulation.active_checkpoint(), simulation.respawns());
    ImGui::Text("Input: %s", status.input_mode.c_str());
    if (!status.gamepad_name.empty()) {
        ImGui::Text("Gamepad: %s", status.gamepad_name.c_str());
        const auto &b = status.gamepad_labels;
        const std::string actions = std::string(b.jump) + " jump  |  " + std::string(b.long_jump) +
                                    " + jump: long jump  |  " + std::string(b.walk) + " walk";
        const std::string menus = std::string(b.recenter) + ": center camera  |  " +
                                  std::string(b.respawn) + " respawn  |  " + std::string(b.menu) +
                                  " tuning";
        ImGui::TextUnformatted(actions.c_str());
        ImGui::TextUnformatted(menus.c_str());
    }
    const int minutes = static_cast<int>(status.hours * 60) % (24 * 60);
    ImGui::Text("Time %02d:%02d %s", minutes / 60, minutes % 60,
                status.day_animating ? "(day/night running)" : "(paused, F2 > Lighting)");
    const float momentum = std::max(speed - simulation.player.tuning.run_speed, 0.0F);
    ImGui::Text("Momentum %+.1f m/s   Last hop: %s", static_cast<double>(momentum),
                to_string(s.last_hop));
    ImGui::Text("Wall kick window: %s",
                s.wall_timer > 0 && (!s.wall_used || dot(s.wall_normal, s.last_kick_normal) <= 0.5F)
                    ? "ready"
                    : "--");
    ImGui::Separator();
    if (status.free_camera) {
        ImGui::TextUnformatted("FREE CAMERA  WASD/QE fly  Shift fast  Home reset");
    } else {
        ImGui::TextUnformatted("WASD move  |  Space jump / wall kick  |  Ctrl walk");
        ImGui::TextUnformatted("Shift + jump: long jump  |  R respawn  |  Mouse look");
    }
    ImGui::TextUnformatted("F1 overlay  F2 tuning  F3 free camera  Esc quit");
    if (status.pacing != nullptr && status.pacing->stats().count > 0) {
        const auto stats = status.pacing->stats();
        ImGui::Separator();
        ImGui::Text("Frame %.2f ms  jitter %.2f ms  range %.1f-%.1f ms", stats.mean_ms,
                    stats.deviation_ms, stats.min_ms, stats.max_ms);
        ImGui::Text("Long frames %u  uneven tick frames %u  (last %zu)", stats.long_frames,
                    stats.uneven_ticks, stats.count);
        const auto &values = status.pacing->intervals();
        ImGui::PlotLines("##frames", values.data(), static_cast<int>(values.size()),
                         static_cast<int>(status.pacing->offset()), nullptr, 0.0F,
                         static_cast<float>(std::max(40.0, stats.mean_ms * 2.5)), ImVec2(380, 50));
    }
}

namespace {
void slider(const char *label, float &value, float low, float high, const char *format = "%.2f") {
    ImGui::SliderFloat(label, &value, low, high, format);
}
void angle_slider(const char *label, float &radians, float low_degrees, float high_degrees) {
    ImGui::SliderAngle(label, &radians, low_degrees, high_degrees);
}

void lighting_section(LightingControls &lighting) {
    if (!ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    auto &cycle = lighting.cycle;
    slider("Time of day (h)", cycle.hours, 0, 24, "%.2f");
    ImGui::Checkbox("Animate day/night", &cycle.animate);
    slider("Day length (s)", cycle.day_seconds, 20, 900, "%.0f");
    slider("Exposure scale", cycle.exposure_scale, 0.25F, 4);
    int quality = static_cast<int>(lighting.shadows.quality);
    if (ImGui::Combo("Shadow quality", &quality, "Low\0Medium\0High\0"))
        lighting.shadows.quality = static_cast<ShadowQuality>(quality);
    slider("Depth bias", lighting.shadows.constant_bias, 0, 16);
    slider("Slope bias", lighting.shadows.slope_bias, 0, 8);
    slider("Normal offset (texels)", lighting.shadows.normal_offset, 0, 6);
    ImGui::Checkbox("Local lights", &lighting.local_lights);
    ImGui::SameLine();
    ImGui::Checkbox("Bulbs", &lighting.bulbs);
    ImGui::Checkbox("Contact (blob) shadow", &lighting.blob);
    slider("Blob strength", lighting.blob_strength, 0, 1);
    if (ImGui::Button("Reset lighting")) {
        const float hours = lighting.cycle.hours;
        lighting = {};
        lighting.cycle.hours = hours;
    }
}
} // namespace

void tuning_window(Simulation &simulation, const TuningPanels &panels, bool &open) {
    auto &lighting = panels.lighting;
    auto &gamepad = panels.gamepad;
    if (!open)
        return;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 420, 18), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Tuning", &open)) {
        ImGui::End();
        return;
    }
    auto &t = simulation.player.tuning;
    auto &c = simulation.camera.tuning;
    ImGui::TextWrapped("Edits apply from the next tick. Replays assume default tuning.");
    lighting_section(lighting);
    if (ImGui::Button("Reset movement"))
        t = {};
    ImGui::SameLine();
    if (ImGui::Button("Reset camera"))
        c = {};
    ImGui::SameLine();
    if (ImGui::Button("Respawn"))
        simulation.respawn();
    if (ImGui::CollapsingHeader("Ground", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("Run speed", t.run_speed, 2, 20);
        slider("Walk fraction", t.walk_fraction, 0.1F, 1);
        slider("Acceleration", t.ground_acceleration, 2, 80);
        slider("Deceleration", t.ground_deceleration, 2, 80);
        slider("Turn rate (slow)", t.turn_rate_slow, 1, 40);
        slider("Turn rate (fast)", t.turn_rate_fast, 0.5F, 30);
        slider("Turn slowdown", t.turn_slowdown, 0, 1);
        angle_slider("Skid angle", t.skid_angle, 90, 180);
        slider("Skid min speed", t.skid_min_speed, 0, 15);
        slider("Skid deceleration", t.skid_deceleration, 5, 120);
    }
    if (ImGui::CollapsingHeader("Jump and air", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("Jump height", t.jump_height, 0.5F, 6);
        slider("Time to apex", t.jump_time_to_apex, 0.15F, 0.8F, "%.3f s");
        slider("Speed bonus", t.jump_speed_bonus, 0, 0.5F);
        slider("Release gravity x", t.release_gravity_scale, 1, 6);
        slider("Fall gravity x", t.fall_gravity_scale, 0.5F, 4);
        slider("Terminal speed", t.terminal_speed, 5, 80);
        slider("Air acceleration", t.air_acceleration, 0, 40);
        slider("Air speed", t.air_speed, 0, 20);
        slider("Air turn rate", t.air_turn_rate, 0, 20);
        slider("Coyote time", t.coyote_time, 0, 0.3F, "%.3f s");
        slider("Jump buffer", t.jump_buffer, 0, 0.3F, "%.3f s");
        ImGui::Text("Derived: gravity %.1f m/s^2, launch %.2f m/s",
                    static_cast<double>(t.gravity()), static_cast<double>(t.jump_velocity()));
    }
    if (ImGui::CollapsingHeader("Momentum", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("Max speed", t.max_speed, 5, 40);
        slider("Hop window", t.hop_window, 0, 0.2F, "%.3f s");
        slider("Early hop keeps", t.early_hop_retention, 0, 1);
        slider("Ground friction", t.overspeed_friction, 0, 60);
        slider("Slope momentum", t.slope_momentum, 0, 2);
        slider("Air drag", t.air_drag, 0, 10);
        ImGui::TextUnformatted("Momentum = speed above run speed. Clean hop: jump within the");
        ImGui::TextUnformatted("hop window of landing. Long jumps and downhill running build it.");
    }
    if (ImGui::CollapsingHeader("Wall / long jump", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("Long jump min speed", t.long_jump_min_speed, 0, 15);
        slider("Long jump speed x", t.long_jump_speed_scale, 1, 3);
        slider("Long jump chain boost", t.long_jump_chain_boost, 0, 8);
        slider("Long jump up speed", t.long_jump_up_speed, 2, 16);
        slider("Wall kick speed", t.wall_jump_speed, 2, 16);
        slider("Wall kick up speed", t.wall_jump_up_speed, 2, 18);
        slider("Wall contact grace", t.wall_contact_grace, 0, 0.3F, "%.3f s");
        slider("Wall recovery", t.wall_recovery_time, 0, 0.4F, "%.3f s");
    }
    if (ImGui::CollapsingHeader("Collision")) {
        angle_slider("Max slope", t.collision.max_slope, 10, 80);
        slider("Step height", t.collision.step_height, 0, 1);
        slider("Ground snap", t.collision.ground_snap, 0, 1);
        ImGui::Checkbox("Show collision capsule", &panels.diagnostics.show_capsule);
    }
    if (ImGui::CollapsingHeader("Gamepad", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("Move dead zone", gamepad.move_dead_zone, 0, 0.5F);
        slider("Look dead zone", gamepad.look_dead_zone, 0, 0.5F);
        ImGui::TextUnformatted("D-pad / left stick: navigate; South: select; East: back.");
        ImGui::TextUnformatted("Start closes tuning. Camera sensitivity is below.");
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("Distance", c.distance, 2, 16);
        slider("Focus height", c.focus_height, 0, 3);
        slider("Mouse sensitivity", c.mouse_sensitivity, 0.0005F, 0.01F, "%.4f");
        slider("Stick yaw speed", c.stick_yaw_speed, 0.5F, 8);
        slider("Stick pitch speed", c.stick_pitch_speed, 0.5F, 8);
        ImGui::Checkbox("Invert Y", &c.invert_y);
        angle_slider("Min pitch", c.min_pitch, -85, 0);
        angle_slider("Max pitch", c.max_pitch, 0, 85);
        slider("Follow half-life", c.follow_half_life, 0, 0.5F, "%.3f s");
        slider("Vertical half-life", c.vertical_half_life, 0, 1, "%.3f s");
        slider("Air half-life", c.air_half_life, 0, 2, "%.3f s");
        slider("Air window", c.air_window, 0, 5);
        slider("Recover half-life", c.recover_half_life, 0, 2, "%.3f s");
    }
    if (ImGui::CollapsingHeader("Diagnostics: frame pacing")) {
        auto &d = panels.diagnostics;
        ImGui::Text("Display refresh: %.2f Hz", static_cast<double>(d.refresh_hz));
        if (d.display_pacing_supported) {
            ImGui::Checkbox("Pace frames to the display", &d.display_pacing.enabled);
            ImGui::TextUnformatted("Frames queued:");
            for (const std::uint32_t queued : {1U, 2U}) {
                ImGui::SameLine();
                const auto label = std::to_string(queued);
                if (ImGui::RadioButton(label.c_str(), d.display_pacing.queued_frames == queued))
                    d.display_pacing.queued_frames = queued;
            }
        } else {
            ImGui::TextUnformatted("Display pacing unavailable (no VK_KHR_present_wait).");
        }
        ImGui::Checkbox("Snap frame time to refresh", &d.snap_to_refresh);
        ImGui::TextUnformatted("Experiments (each is logged):");
        d.wait_idle = ImGui::Button("Wait for GPU idle");
        ImGui::SameLine();
        d.rebuild_shadow_map = ImGui::Button("Rebuild shadow map");
        ImGui::SameLine();
        d.rebuild_swapchain = ImGui::Button("Recreate swapchain");
        d.toggle_log = ImGui::Button(d.logging ? "Stop frame log" : "Start frame log");
        if (!d.log_path.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(d.log_path.c_str());
        }
    }
    ImGui::End();
}

} // namespace mj::game
