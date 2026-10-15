#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#ifdef __clang__
// doctest 2.4.12 includes the retired <ciso646> header when detecting libc++.
// Scope this compatibility suppression to the upstream header, not project code.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#endif
#include <doctest/doctest.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <mj/core/launch_options.hpp>
#include <mj/render/device_requirements.hpp>

#include <array>

TEST_CASE("command line rejects unsupported features instead of silently ignoring them") {
    CHECK(mj::parse_launch_options({}).has_value());
    const std::array help{std::string_view{"--help"}};
    REQUIRE(mj::parse_launch_options(help).has_value());
    CHECK(mj::parse_launch_options(help)->help);
    const std::array smoke{std::string_view{"--smoke-test"}};
    REQUIRE(mj::parse_launch_options(smoke).has_value());
    CHECK(mj::parse_launch_options(smoke)->smoke_test);
    const std::array unknown{std::string_view{"--capture-frame"}};
    const auto result = mj::parse_launch_options(unknown);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("--capture-frame") != std::string::npos);
}

TEST_CASE("device selection enforces every mandatory Vulkan capability") {
    const mj::DeviceSupport compatible{1, 3, true, true, true, true, true, true, true};
    CHECK(mj::device_rejection_reason(compatible).empty());

    auto support = compatible;
    support.api_minor = 2;
    CHECK_FALSE(mj::device_rejection_reason(support).empty());
    support = compatible;
    support.api_minor = 4;
    CHECK(mj::device_rejection_reason(support).empty());

    constexpr std::array mandatory{&mj::DeviceSupport::graphics_and_present_queue,
                                   &mj::DeviceSupport::swapchain,
                                   &mj::DeviceSupport::surface_formats,
                                   &mj::DeviceSupport::present_modes,
                                   &mj::DeviceSupport::dynamic_rendering,
                                   &mj::DeviceSupport::synchronization2,
                                   &mj::DeviceSupport::timeline_semaphore};
    for (const auto capability : mandatory) {
        support = compatible;
        support.*capability = false;
        CHECK_FALSE(mj::device_rejection_reason(support).empty());
    }
}
