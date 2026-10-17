#include "pause_menu.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace mj::game {
namespace {
const ImVec4 k_gold{1, 0.8F, 0.38F, 1};
const ImVec4 k_sky{0.63F, 0.88F, 0.88F, 1};
const ImVec4 k_dim{0.64F, 0.7F, 0.75F, 1};

void section(const char *title, std::string_view description) {
    ImGui::SetWindowFontScale(1.5F);
    ImGui::TextColored(k_sky, "%s", title);
    ImGui::SetWindowFontScale(1.15F);
    ImGui::PushStyleColor(ImGuiCol_Text, k_dim);
    ImGui::TextWrapped("%.*s", static_cast<int>(description.size()), description.data());
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

bool button(const char *label) {
    return ImGui::Button(label, ImVec2(-1, 38));
}

void hint(std::string_view text) {
    if (text.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, k_dim);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void controls(const GamepadLabels &labels, bool gamepad) {
    struct Row {
        const char *action;
        const char *keys;
        std::string pad;
    };
    const std::string jump(labels.jump);
    const Row rows[] = {
        {"Move", "W A S D", "Left stick"},
        {"Look", "Mouse", "Right stick"},
        {"Jump / wall kick", "Space", jump},
        {"Long jump", "Shift + Space", std::string(labels.long_jump) + " + " + jump},
        {"Dive", "E / left click", std::string(labels.dive)},
        {"Roll out of a dive", "Space", jump},
        {"Walk", "Ctrl", std::string(labels.walk)},
        {"Recenter camera", "Middle click", std::string(labels.recenter)},
        {"Last checkpoint", "R", std::string(labels.respawn)},
        {"Pause / resume", "Escape", std::string(labels.menu)},
    };
    if (ImGui::BeginTable("Controls", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action", 0, 1.25F);
        ImGui::TableSetupColumn("Keyboard", 0, 1);
        ImGui::TableSetupColumn(gamepad ? "Controller" : "Controller*", 0, 1);
        ImGui::TableHeadersRow();
        for (const auto &row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", row.action);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", row.keys);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", row.pad.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    hint("Jump again as you land for a double, then a triple jump. Hold jump to go higher. "
         "Wall kicks work on purple walls.");
    if (!gamepad)
        hint("* Connect a controller to see its button names.");
}
} // namespace

void PauseMenu::open(MenuPage page) {
    m_open = true;
    m_page = page;
    m_focus = true;
    m_sync_display = true;
    m_scroll_to_top = true;
    m_focus_quit = page == Page::quit;
}

void PauseMenu::close() {
    m_open = false;
}

MenuChoice PauseMenu::draw(const MenuView &view, PlayerOptions &options, AudioControls &audio,
                           DisplayOptions &display_options) {
    if (!m_open)
        return {};
    if (std::exchange(m_sync_display, false))
        m_display = display_options;
    MenuChoice choice;
    const auto display = ImGui::GetIO().DisplaySize;
    const float width = std::min(1000.0F, display.x - 24);
    const float height = std::min(660.0F, display.y - 24);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display, IM_COL32(4, 10, 18, 175));
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5F, display.y * 0.5F), ImGuiCond_Always,
                            ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    ImGui::Begin("Pause", nullptr, flags);
    ImGui::SetWindowFontScale(1.35F);
    ImGui::TextColored(k_gold, "MICROJUMP");
    ImGui::SameLine();
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored(k_dim, "  /  PAUSED");
    if (!view.location.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(k_sky, "  %.*s", static_cast<int>(view.location.size()),
                           view.location.data());
    }
    ImGui::Separator();
    const float footer = 40;
    const float sidebar = width < 760 ? 132.0F : 180.0F;
    ImGui::BeginChild("Navigation", ImVec2(sidebar, -footer), ImGuiChildFlags_NavFlattened);
    ImGui::SetWindowFontScale(1.15F);
    const auto nav = [&](const char *label, Page page) {
        // Full-width Selectables expand into horizontal spacing. Keep their navigation
        // rect inside this unpadded child, so gamepad Right can leave the sidebar.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10));
        if (ImGui::Selectable(label, m_page == page, 0, ImVec2(0, 36))) {
            m_page = page;
            m_scroll_to_top = true;
            m_focus_quit = page == Page::quit;
        }
        ImGui::PopStyleVar();
    };
    if (std::exchange(m_focus, false))
        ImGui::SetKeyboardFocusHere();
    if (button("Resume"))
        choice.kind = MenuChoice::Kind::resume;
    ImGui::Spacing();
    nav("Places", Page::places);
    nav("Options", Page::options);
    nav("Controls", Page::controls);
    nav("Quit game", Page::quit);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Content", ImVec2(0, -footer),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    if (std::exchange(m_scroll_to_top, false))
        ImGui::SetScrollY(0);
    ImGui::SetWindowFontScale(1.15F);
    switch (m_page) {
    case Page::main:
        section("Take a breath.", "Your next jump can wait.");
        ImGui::TextWrapped("Explore the places you've found, make the game feel right in Options, "
                           "or brush up on your moves.");
        ImGui::Spacing();
        hint(view.saving ? "Sparks and checkpoints save automatically."
                         : "Finds in this run last until the game closes.");
        break;
    case Page::places:
        section("Places", "A whole world, one leap at a time.");
        for (std::size_t i = 0; i < view.places.size(); ++i) {
            const auto &place = view.places[i];
            const auto &status = view.status[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginDisabled(!status.visited || !view.can_travel || status.here);
            if (button(status.visited ? place.title.c_str() : "Undiscovered place")) {
                choice.kind = MenuChoice::Kind::travel;
                choice.place = i;
            }
            ImGui::EndDisabled();
            if (!status.visited) {
                hint("Find its gate to begin exploring.");
            } else {
                ImGui::TextColored(status.found == place.sparks ? k_gold : k_dim,
                                   "%zu / %zu sparks%s", status.found, place.sparks,
                                   status.here ? "   /   You are here" : "");
                if (place.sparks > 0)
                    ImGui::ProgressBar(static_cast<float>(status.found) /
                                           static_cast<float>(place.sparks),
                                       ImVec2(-1, 4), "");
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
        hint(view.can_travel ? "Travel returns you to your last checkpoint there."
                             : "Travel is unavailable while recording or replaying.");
        break;
    case Page::options:
        section("Options", "Your screen. Your sound. Your way to play.");
        if (ImGui::BeginTabBar("Settings tabs")) {
            if (ImGui::BeginTabItem("Display")) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 6));
                ImGui::Spacing();
                const float field_start = ImGui::GetCursorPosX() +
                                          std::min(160.0F, ImGui::GetContentRegionAvail().x * 0.4F);
                const auto label_row = [&](const char *label) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(field_start);
                    ImGui::SetNextItemWidth(-1);
                };
                label_row("Window mode");
                ImGui::SetNextItemWidth(-1);
                int mode = m_display.fullscreen ? 1 : 0;
                if (ImGui::Combo("##Window mode", &mode, "Windowed\0Borderless fullscreen\0"))
                    m_display.fullscreen = mode == 1;
                ImGui::Spacing();
                ImGui::BeginDisabled(m_display.fullscreen);
                const auto resolution =
                    std::to_string(m_display.width) + " x " + std::to_string(m_display.height);
                label_row("Resolution");
                if (ImGui::BeginCombo("##Resolution", resolution.c_str())) {
                    for (const auto size : view.resolutions) {
                        const auto label =
                            std::to_string(size.width) + " x " + std::to_string(size.height);
                        const bool selected =
                            size.width == static_cast<unsigned>(m_display.width) &&
                            size.height == static_cast<unsigned>(m_display.height);
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            m_display.width = static_cast<int>(size.width);
                            m_display.height = static_cast<int>(size.height);
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                if (m_display.fullscreen)
                    hint("Fullscreen uses your desktop's resolution.");
                ImGui::Checkbox("VSync", &m_display.vsync);
                if (!view.vsync_off_supported)
                    hint("This display requires VSync. The game will keep it enabled.");
                else
                    hint("Match the display's refresh to prevent tearing.");
                const std::array limits{30, 60, 120, 0};
                const auto found = std::find(limits.begin(), limits.end(), m_display.frame_limit);
                int limit = found == limits.end() ? 3 : static_cast<int>(found - limits.begin());
                label_row("Frame limit");
                if (ImGui::Combo("##Frame limit", &limit,
                                 "30 FPS\0"
                                 "60 FPS\0"
                                 "120 FPS\0Unlimited\0"))
                    m_display.frame_limit = limits[static_cast<std::size_t>(limit)];
                hint("VSync can keep the frame rate below this limit.");
                ImGui::Spacing();
                bool applied = false;
                ImGui::BeginDisabled(m_display == display_options);
                if (button("Apply display settings")) {
                    display_options = m_display;
                    m_sync_display = true; // Main may fit the window to a smaller desktop.
                    applied = true;
                }
                ImGui::EndDisabled();
                // Apply becomes disabled on the next frame. Keep keyboard/controller
                // focus on an enabled control so Tab can continue through the menu.
                if (applied)
                    ImGui::SetKeyboardFocusHere();
                if (m_display != display_options)
                    hint("You have unapplied changes.");
                if (button("Reset display defaults"))
                    m_display = {};
                ImGui::PopStyleVar();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Sound")) {
                ImGui::Spacing();
                ImGui::BeginDisabled(!audio.available);
                ImGui::TextUnformatted("Master volume");
                ImGui::SetNextItemWidth(-1);
                float percent = audio.volume * 100;
                if (ImGui::SliderFloat("##Volume", &percent, 0, 100, "%.0f%%",
                                       ImGuiSliderFlags_AlwaysClamp))
                    audio.volume = percent / 100;
                ImGui::Checkbox("Mute sound", &audio.muted);
                ImGui::EndDisabled();
                if (!audio.available)
                    hint("No audio device is available for this run.");
                ImGui::Spacing();
                hint("Sound and camera changes apply immediately.");
                if (button("Reset sound defaults")) {
                    audio.volume = 0.8F;
                    audio.muted = false;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Camera")) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Mouse look speed");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##Mouse look", &options.mouse_sensitivity, 0.25F, 3, "x%.2f",
                                   ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextUnformatted("Controller look speed");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##Stick look", &options.stick_speed, 0.25F, 3, "x%.2f",
                                   ImGuiSliderFlags_AlwaysClamp);
                ImGui::Checkbox("Invert camera up and down", &options.invert_y);
                ImGui::Spacing();
                if (button("Reset camera defaults"))
                    options = {};
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Spacing();
        hint(view.settings_status);
        if (ImGui::Button("Advanced tuning (F2)"))
            choice.kind = MenuChoice::Kind::tuning;
        break;
    case Page::controls:
        section("Make your move", "A little momentum goes a long way.");
        controls(view.labels, view.gamepad);
        break;
    case Page::quit:
        section("Done exploring?", view.saving ? "Your sparks and checkpoints are kept."
                                               : "This run's finds will be lost when you quit.");
        if (std::exchange(m_focus_quit, false))
            ImGui::SetKeyboardFocusHere();
        if (button("Keep playing"))
            choice.kind = MenuChoice::Kind::resume;
        ImGui::Spacing();
        if (button("Quit microjump"))
            choice.kind = MenuChoice::Kind::quit;
        break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::SetWindowFontScale(1);
    if (view.gamepad) {
        const auto prompt = "D-pad: navigate   /   " + std::string(view.labels.jump) +
                            ": choose   /   " + std::string(view.labels.menu) + ": resume";
        hint(prompt);
    } else {
        hint("Arrows: move   /   Tab: next   /   Enter: choose   /   Esc: resume");
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    return choice;
}

} // namespace mj::game
