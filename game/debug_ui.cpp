#include "debug_ui.hpp"

#include <imgui.h>

namespace mj::game {

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
    ImGui::Text("Input: %s%s", status.input_mode.c_str(), status.gamepad ? "   |   Gamepad" : "");
    const int minutes = static_cast<int>(status.hours * 60) % (24 * 60);
    ImGui::Text("Time %02d:%02d %s", minutes / 60, minutes % 60,
                status.day_animating ? "(day/night running)" : "(paused, F2 > Lighting)");
    ImGui::Separator();
    if (status.free_camera) {
        ImGui::TextUnformatted("FREE CAMERA  WASD/QE fly  Shift fast  Home reset");
    } else {
        ImGui::TextUnformatted("WASD move  |  Space jump  |  Ctrl walk  |  Mouse look");
        ImGui::TextUnformatted("R respawn  |  Pad: sticks, A jump, Back respawn");
    }
    ImGui::TextUnformatted("F1 overlay  F2 tuning  F3 free camera  Esc quit");
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

void tuning_window(Simulation &simulation, LightingControls &lighting, bool &open) {
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
    if (ImGui::CollapsingHeader("Collision")) {
        angle_slider("Max slope", t.collision.max_slope, 10, 80);
        slider("Step height", t.collision.step_height, 0, 1);
        slider("Ground snap", t.collision.ground_snap, 0, 1);
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
    ImGui::End();
}

} // namespace mj::game
