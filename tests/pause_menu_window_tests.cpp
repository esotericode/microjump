#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "test_framework.hpp"
#include "virtual_gamepad.hpp"

#include <pause_menu.hpp>

#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace {
// Inspect semantic widget IDs, not screen coordinates. All focus changes and
// activations still travel through real keyboard or SDL virtual-controller input.
struct MenuHarness {
    mj::Window &window;
    mj::Renderer &renderer;
    mj::test::VirtualPad &pad;
    mj::game::PauseMenu menu;
    mj::game::PlayerOptions player;
    mj::game::AudioControls audio;
    mj::game::DisplayOptions display;
    mj::game::MenuChoice choice;
    const std::vector<mj::game::Place> places{
        {"playground.json", "microjump-playground", "The Playground", 16},
        {"lantern-isles.json", "lantern-isles", "The Lantern Isles", 16},
        {"clockwork-garden.json", "clockwork-garden", "The Clockwork Garden", 16}};
    const std::vector<mj::game::PlaceStatus> status{
        {.visited = true, .here = true, .found = 3}, {.visited = true, .found = 8}, {}};
    const std::array<mj::PixelSize, 2> resolutions{mj::PixelSize{640, 360},
                                                   mj::PixelSize{1280, 720}};
    mj::GamepadLabels labels;

    MenuHarness(mj::Window &test_window, mj::Renderer &test_renderer,
                mj::test::VirtualPad &test_pad)
        : window(test_window), renderer(test_renderer), pad(test_pad) {
        audio.available = true;
    }

    void frame(const std::filesystem::path *capture = nullptr) {
        bool presented = false;
        for (int attempt = 0; attempt < 4 && !presented; ++attempt) {
            const auto input =
                window.poll_events([&](const SDL_Event &event) { renderer.process_event(event); },
                                   {.gamepad_gameplay = !menu.is_open()});
            REQUIRE(input.focused);
            REQUIRE(input.gamepad_connected);
            labels = input.gamepad_labels;
            const std::string settings_message =
                "Preferences saved. This suffix is not part of it.";
            const auto message = std::string_view(settings_message).substr(0, 18);
            const std::function<void()> ui = [&] {
                const auto next = menu.draw({places, status, labels, true, true, true,
                                             "The Clockwork Garden", resolutions, true, message},
                                            player, audio, display);
                if (next.kind != mj::game::MenuChoice::Kind::none)
                    choice = next;
            };
            const std::array instances{
                mj::DrawInstance{.mesh = {0}, .model = mj::yaw_translation(0, {}), .grid = 0.3F}};
            mj::FrameView view;
            view.instances = instances;
            view.environment.sky_zenith = {0.08F, 0.12F, 0.2F};
            view.environment.sky_horizon = {0.18F, 0.28F, 0.35F};
            view.environment.ground = {0.05F, 0.06F, 0.08F};
            view.overlay = false;
            view.keyboard_navigation = menu.is_open();
            view.gamepad_navigation = menu.is_open();
            view.debug_windows = &ui;
            view.capture = capture;
            const auto drawn = renderer.draw(view);
            INFO((drawn ? "drawn" : drawn.error().message));
            REQUIRE(drawn);
            presented = *drawn;
            SDL_Delay(8);
        }
        REQUIRE(presented);
    }

    void settle() {
        frame();
        frame();
    }

    void key(ImGuiKey which) {
        ImGui::GetIO().AddKeyEvent(which, true);
        frame();
        ImGui::GetIO().AddKeyEvent(which, false);
        frame();
    }

    void controller(SDL_GamepadButton which) {
        pad.button(which, true);
        frame();
        pad.button(which, false);
        frame();
    }

    [[nodiscard]] static ImGuiWindow *child(std::string_view name) {
        const auto prefix = std::string("Pause/") + std::string(name) + "_";
        for (auto *candidate : ImGui::GetCurrentContext()->Windows) {
            if (candidate->Active && std::string_view(candidate->Name).starts_with(prefix))
                return candidate;
        }
        FAIL("Pause menu child was not drawn: " << name);
        return nullptr;
    }

    [[nodiscard]] static ImGuiID item(std::string_view panel, const char *label) {
        return child(panel)->GetID(label);
    }

    [[nodiscard]] static ImGuiTabBar *tabs() {
        const auto id = item("Content", "Settings tabs");
        auto *bar = ImGui::GetCurrentContext()->TabBars.GetByKey(id);
        REQUIRE(bar != nullptr);
        return bar;
    }

    [[nodiscard]] static ImGuiID tab(const char *label) {
        auto *bar = tabs();
        for (auto &entry : bar->Tabs) {
            if (std::string_view(ImGui::TabBarGetTabName(bar, &entry)) == label)
                return entry.ID;
        }
        FAIL("Settings tab was not drawn: " << label);
        return 0;
    }

    [[nodiscard]] static ImGuiID setting(const char *page, const char *label) {
        return ImHashStr(label, 0, tab(page));
    }

    void focus(ImGuiID target, std::string_view label) {
        // Tab crosses flattened child boundaries and scrolls clipped controls into view.
        // A bounded search tolerates adding or reordering controls without hiding a trap.
        for (int attempt = 0; attempt < 48; ++attempt) {
            if (ImGui::GetCurrentContext()->NavId == target)
                return;
            key(ImGuiKey_Tab);
        }
        CAPTURE(target);
        CAPTURE(ImGui::GetCurrentContext()->NavId);
        CAPTURE(label);
        CAPTURE(ImGui::GetCurrentContext()->ActiveId);
        INFO("Navigation window: " << std::string(ImGui::GetCurrentContext()->NavWindow != nullptr
                                                      ? ImGui::GetCurrentContext()->NavWindow->Name
                                                      : "none"));
        FAIL("Keyboard navigation could not reach the requested menu control");
    }

    void select_tab(const char *label) {
        focus(tab(label), label);
        key(ImGuiKey_Enter);
        settle();
        CHECK(tabs()->SelectedTabId == tab(label));
    }

    static void check_layout() {
        const auto screen = ImGui::GetIO().DisplaySize;
        const auto *pause = ImGui::FindWindowByName("Pause");
        REQUIRE(pause != nullptr);
        CHECK(pause->Pos.x >= 0.0F);
        CHECK(pause->Pos.y >= 0.0F);
        CHECK(pause->Pos.x + pause->Size.x <= screen.x + 1);
        CHECK(pause->Pos.y + pause->Size.y <= screen.y + 1);
        const auto *content = child("Content");
        CHECK(content->Size.x > 200.0F);
        CHECK(content->Size.y > 100.0F);
        CHECK(content->Pos.x + content->Size.x <= pause->Pos.x + pause->Size.x);
        CHECK(content->Pos.y + content->Size.y <= pause->Pos.y + pause->Size.y);
        REQUIRE(ImGui::GetDrawData() != nullptr);
        CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    }

    void capture(std::string_view page, std::uint32_t width) {
        const auto path = mj::Window::executable_directory() / "../tests/captures" /
                          ("pause-" + std::string(page) + "-" + std::to_string(width) + ".png");
        std::filesystem::remove(path);
        frame(&path);
        REQUIRE(std::filesystem::exists(path));
        CHECK(std::filesystem::file_size(path) > 100);
        check_layout();
    }
};
} // namespace

TEST_CASE(
    "pause menu settings are reachable with keyboard and controller at small and large sizes") {
    mj::test::virtual_gamepads_only();
    mj::ValidationLog validation;
    {
        auto window = mj::Window::create();
        REQUIRE(window);
        auto context = mj::VulkanContext::create(**window, validation);
        REQUIRE(context);
        const auto geometry = mj::game::build_geometry(mj::game::make_movement_yard());
        const std::array meshes{geometry.mesh};
        auto renderer = mj::Renderer::create(**window, **context, meshes);
        REQUIRE(renderer);
        mj::test::VirtualPad pad("microjump pause-menu controller");
        REQUIRE(SDL_RaiseWindow((*window)->native_handle()));
        REQUIRE(SDL_SyncWindow((*window)->native_handle()));
        bool focused = false;
        for (int attempt = 0; attempt < 60 && !focused; ++attempt) {
            const auto input = (*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); }, {});
            mj::FrameView view;
            view.overlay = false;
            REQUIRE((*renderer)->draw(view));
            focused = input.focused;
            SDL_Delay(17);
        }
        REQUIRE(focused);

        MenuHarness ui{**window, **renderer, pad};
        for (const auto size : ui.resolutions) {
            CAPTURE(size.width);
            CAPTURE(size.height);
            ui.menu.close();
            ui.frame();
            REQUIRE((*window)->resize(static_cast<int>(size.width), static_cast<int>(size.height)));
            ui.display = {};
            ui.audio.muted = false;
            ui.player = {};
            ui.choice = {};
            ui.menu.open();
            ui.settle();
            ui.focus(MenuHarness::item("Navigation", "Options"), "Options sidebar");
            ui.key(ImGuiKey_Enter);
            ui.settle();
            // Selecting a page must preserve its navigation focus, not jump to Resume.
            CHECK(ImGui::GetCurrentContext()->NavId == MenuHarness::item("Navigation", "Options"));
            if (size.width == 640)
                ui.key(ImGuiKey_RightArrow);
            else
                ui.controller(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            INFO("After Right: NavId="
                 << ImGui::GetCurrentContext()->NavId
                 << " ActiveId=" << ImGui::GetCurrentContext()->ActiveId << " window="
                 << std::string(ImGui::GetCurrentContext()->NavWindow != nullptr
                                    ? ImGui::GetCurrentContext()->NavWindow->Name
                                    : "none")
                 << " gamepad="
                 << ((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_HasGamepad) != 0));
            REQUIRE(ImGui::GetCurrentContext()->NavWindow == MenuHarness::child("Content"));

            ui.select_tab("Display");
            ui.focus(MenuHarness::setting("Display", "##Frame limit"), "Frame limit");
            ui.key(ImGuiKey_Enter);
            REQUIRE_FALSE(ImGui::GetCurrentContext()->OpenPopupStack.empty());
            ui.key(ImGuiKey_UpArrow); // Unlimited -> 120.
            ui.key(ImGuiKey_UpArrow); // 120 -> 60.
            ui.key(ImGuiKey_Enter);
            CHECK(ui.display.frame_limit == 0); // Changes wait for Apply.
            ui.focus(MenuHarness::setting("Display", "Apply display settings"), "Apply display");
            ui.key(ImGuiKey_Enter);
            CHECK(ui.display.frame_limit == 60);
            CHECK(ImGui::GetCurrentContext()->NavId ==
                  MenuHarness::setting("Display", "Reset display defaults"));
            ui.capture("display", size.width);

            ui.select_tab("Sound");
            ui.focus(MenuHarness::setting("Sound", "Mute sound"), "Mute sound");
            ui.key(ImGuiKey_Enter);
            CHECK(ui.audio.muted);
            ui.select_tab("Camera");
            ui.focus(MenuHarness::setting("Camera", "Invert camera up and down"), "Invert camera");
            ui.controller(SDL_GAMEPAD_BUTTON_SOUTH);
            CHECK(ui.player.invert_y);
            ui.focus(MenuHarness::setting("Camera", "Reset camera defaults"), "Reset camera");
            ui.key(ImGuiKey_Enter);
            CHECK_FALSE(ui.player.invert_y);

            ui.menu.open(mj::MenuPage::controls);
            ui.settle();
            ui.capture("controls", size.width);

            ui.choice = {};
            ui.focus(MenuHarness::item("Navigation", "Quit game"), "Quit sidebar");
            ui.key(ImGuiKey_Enter);
            CHECK(ui.choice.kind == mj::game::MenuChoice::Kind::none);
            if (size.width == 640) {
                // This row is below the initial fold; a controller must still be able
                // to leave the sidebar after navigation has scrolled it into view.
                ui.controller(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
                REQUIRE(ImGui::GetCurrentContext()->NavWindow == MenuHarness::child("Content"));
            }
            ui.focus(MenuHarness::item("Content", "Quit microjump"), "Confirm quit");
            ui.key(ImGuiKey_Enter);
            CHECK(ui.choice.kind == mj::game::MenuChoice::Kind::quit);
        }
    }
    CHECK(validation.error_count == 0);
}
