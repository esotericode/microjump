#include "pause_menu.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <utility>

namespace mj::game {
namespace {
const ImVec4 k_gold{1, 0.79F, 0.3F, 1};
const ImVec4 k_sky{0.72F, 0.9F, 1, 1};
const ImVec4 k_dim{0.62F, 0.64F, 0.7F, 1};
constexpr float k_button_width = 360;
constexpr float k_scale = 1.3F; // The menu reads from the sofa, not just the desk.

bool button(const char *label) {
    return ImGui::Button(label, ImVec2(k_button_width, 0));
}

void heading(const char *text) {
    ImGui::SetWindowFontScale(k_scale * 1.5F);
    ImGui::TextColored(k_sky, "%s", text);
    ImGui::SetWindowFontScale(k_scale);
    ImGui::Separator();
    ImGui::Spacing();
}

// Two columns of what to press: keyboard and mouse, and the connected controller.
void controls_table(const GamepadLabels &labels, bool gamepad) {
    struct Row {
        const char *action;
        const char *keys;
        std::string pad;
    };
    const std::string jump(labels.jump);
    const Row rows[] = {
        {"Move", "W A S D", "Left stick"},
        {"Look", "Mouse", "Right stick"},
        {"Jump (again on landing: double, triple)", "Space", jump},
        {"Wall kick", "Space at a purple wall", jump + " at a purple wall"},
        {"Long jump", "Shift + Space while running", std::string(labels.long_jump) + " + " + jump},
        {"Dive (jump to roll out)", "E or left click", std::string(labels.dive)},
        {"Walk", "Ctrl", std::string(labels.walk)},
        {"Recenter camera", "Middle click", std::string(labels.recenter)},
        {"Back to checkpoint", "R", std::string(labels.respawn)},
        {"Pause", "Escape", std::string(labels.menu)},
    };
    if (!ImGui::BeginTable("Controls", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("Action");
    ImGui::TableSetupColumn("Keyboard and mouse");
    ImGui::TableSetupColumn(gamepad ? "Controller" : "Controller (none connected)");
    ImGui::TableHeadersRow();
    for (const auto &row : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.action);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.keys);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.pad.c_str());
    }
    ImGui::EndTable();
    ImGui::TextColored(k_dim, "F1 diagnostics, F2 tuning, F3 free camera.");
}
} // namespace

void PauseMenu::open(MenuPage page) {
    m_open = true;
    m_page = page;
    m_focus = true;
}

void PauseMenu::close() {
    m_open = false;
}

MenuChoice PauseMenu::draw(const MenuView &view, PlayerOptions &options, AudioControls &audio) {
    if (!m_open)
        return {};
    MenuChoice choice;
    const auto go = [&](Page page) {
        m_page = page;
        m_focus = true;
    };
    const auto display = ImGui::GetIO().DisplaySize;
    // A veil over the paused world, then the menu in the middle.
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display, IM_COL32(8, 10, 20, 120));
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5F, display.y * 0.5F), ImGuiCond_Always,
                            ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowBgAlpha(0.93F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Pause", nullptr, flags);
    ImGui::SetWindowFontScale(k_scale);
    // Each page starts on its first button, so a controller is ready to choose.
    const auto focus_here = [&] {
        if (std::exchange(m_focus, false))
            ImGui::SetKeyboardFocusHere();
    };
    switch (m_page) {
    case Page::main:
        heading("Paused");
        focus_here();
        if (button("Resume"))
            choice.kind = MenuChoice::Kind::resume;
        if (button("Places"))
            go(Page::places);
        if (button("Options"))
            go(Page::options);
        if (button("Controls"))
            go(Page::controls);
        if (button("Quit"))
            go(Page::quit);
        ImGui::Spacing();
        ImGui::TextColored(k_dim, "%s or Escape to resume", std::string(view.labels.menu).c_str());
        break;
    case Page::places: {
        heading("Places");
        focus_here(); // On the first place: the starting level is always known.
        for (std::size_t i = 0; i < view.places.size(); ++i) {
            const auto &place = view.places[i];
            const auto &status = view.status[i];
            ImGui::PushID(static_cast<int>(i));
            if (!status.visited) {
                ImGui::BeginDisabled();
                button("? ? ?");
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextColored(k_dim, "Find its gate");
            } else {
                const bool travel = view.can_travel && !status.here;
                if (!travel)
                    ImGui::BeginDisabled();
                if (button(place.title.c_str())) {
                    choice.kind = MenuChoice::Kind::travel;
                    choice.place = i;
                }
                if (!travel)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (place.sparks > 0)
                    ImGui::TextColored(status.found == place.sparks ? k_gold : k_sky,
                                       "%zu / %zu sparks%s", status.found, place.sparks,
                                       status.here ? "  (here)" : "");
                else
                    ImGui::TextColored(k_dim, "%s", status.here ? "(here)" : "");
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::TextColored(k_dim, view.can_travel
                                      ? "Travel returns you to your last checkpoint there."
                                      : "Travel rests while recording or replaying.");
        if (button("Back"))
            go(Page::main);
        break;
    }
    case Page::options:
        heading("Options");
        ImGui::PushItemWidth(k_button_width);
        if (audio.available) {
            ImGui::SliderFloat("Volume", &audio.volume, 0, 1, "%.2f");
            ImGui::Checkbox("Mute", &audio.muted);
        } else {
            ImGui::TextColored(k_dim, "No sound for this run.");
        }
        ImGui::SliderFloat("Mouse look", &options.mouse_sensitivity, 0.25F, 3, "x%.2f");
        ImGui::SliderFloat("Stick look", &options.stick_speed, 0.25F, 3, "x%.2f");
        ImGui::Checkbox("Invert camera up and down", &options.invert_y);
        ImGui::PopItemWidth();
        ImGui::Spacing();
        focus_here(); // Not on a slider: focusing one opens it for typing.
        if (button("Tuning panel (F2)"))
            choice.kind = MenuChoice::Kind::tuning;
        if (button("Back"))
            go(Page::main);
        break;
    case Page::controls:
        heading("Controls");
        controls_table(view.labels, view.gamepad);
        ImGui::Spacing();
        focus_here();
        if (button("Back"))
            go(Page::main);
        break;
    case Page::quit:
        heading("Quit microjump?");
        ImGui::TextUnformatted(view.saving ? "Your sparks and checkpoints are kept."
                                           : "This run isn't saving: its finds will be lost.");
        ImGui::Spacing();
        focus_here();
        if (button("Keep playing"))
            go(Page::main);
        if (button("Quit"))
            choice.kind = MenuChoice::Kind::quit;
        break;
    }
    ImGui::End();
    return choice;
}

} // namespace mj::game
