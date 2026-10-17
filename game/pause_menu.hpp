#pragma once

#include "debug_ui.hpp"
#include "level.hpp"
#include "level_io.hpp"
#include "settings.hpp"

#include <mj/core/launch_options.hpp>
#include <mj/platform/gamepad.hpp>
#include <mj/platform/window.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mj::game {

// What the menu shows for one place right now.
struct PlaceStatus {
    bool visited = false;
    bool here = false;
    std::size_t found = 0;
};

struct MenuView {
    const std::vector<Place> &places;
    const std::vector<PlaceStatus> &status;
    const GamepadLabels &labels;
    bool gamepad = false;
    bool can_travel = true; // False while recording or replaying: a recording has one level.
    bool saving = true;     // Whether finds outlast the session.
    std::string_view location;
    std::span<const PixelSize> resolutions;
    bool vsync_off_supported = true;
    std::string_view settings_status;
};

struct MenuChoice {
    enum class Kind : std::uint8_t { none, resume, travel, quit, tuning };
    Kind kind = Kind::none;
    std::size_t place = 0;
};

// Escape or Start pauses the game here. The simulation waits while it is open.
class PauseMenu {
  public:
    [[nodiscard]] bool is_open() const {
        return m_open;
    }
    void open(MenuPage page = MenuPage::main);
    void close();
    // Draws the open menu (ImGui) and returns what the player chose this frame.
    MenuChoice draw(const MenuView &view, PlayerOptions &options, AudioControls &audio,
                    DisplayOptions &display);

  private:
    using Page = MenuPage;
    bool m_open = false;
    Page m_page = Page::main;
    bool m_focus = false; // Focus the page's first item on its next draw.
    bool m_sync_display = true;
    bool m_scroll_to_top = true;
    bool m_focus_quit = false;
    DisplayOptions m_display;
};

} // namespace mj::game
