#include "test_framework.hpp"

#include <frame_pacing.hpp>

#include <mj/core/fixed_step.hpp>
#include <mj/core/launch_options.hpp>
#include <mj/render/pacing.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

using mj::game::FrameSample;

TEST_CASE("frame intervals snap to whole display refreshes only when close") {
    using mj::game::snap_to_refresh;
    constexpr double period = 1.0 / 60;
    CHECK(snap_to_refresh(0.0169, {.refresh_hz = 60}) == doctest::Approx(period));
    CHECK(snap_to_refresh(0.0151, {.refresh_hz = 60}) == doctest::Approx(period));
    CHECK(snap_to_refresh(0.0331, {.refresh_hz = 60}) ==
          doctest::Approx(2 * period));                           // A missed vblank.
    CHECK(snap_to_refresh(0.0250, {.refresh_hz = 60}) == 0.0250); // Between refreshes: unchanged.
    CHECK(snap_to_refresh(0.0040, {.refresh_hz = 60}) == 0.0040); // Shorter than one refresh.
    CHECK(snap_to_refresh(0.0169, {.refresh_hz = 0}) == 0.0169);  // Unknown refresh rate.
    CHECK(snap_to_refresh(0.0071, {.refresh_hz = 144}) == doctest::Approx(1.0 / 144));
    CHECK(
        std::isnan(snap_to_refresh(std::numeric_limits<double>::quiet_NaN(), {.refresh_hz = 60})));
}

TEST_CASE("pacing summary reports jitter, missed frames and uneven tick cadence") {
    std::vector<FrameSample> samples;
    for (int i = 0; i < 100; ++i) {
        FrameSample sample;
        sample.delta_ms = i % 2 == 0 ? 16.0 : 17.333;
        sample.ticks = i < 50 ? 2U : (i % 2 == 0 ? 1U : 3U); // Steady, then alternating.
        sample.acquire_ms = 15;
        sample.gpu_ms = i == 0 ? -1 : 4;
        samples.push_back(sample);
    }
    samples[70].delta_ms = 33.3; // One missed refresh.
    const auto summary = mj::game::summarize(samples);
    CHECK(summary.frames == 100);
    CHECK(summary.long_frames == 1);
    CHECK(summary.uneven_ticks == 50);
    CHECK(summary.max_ms == doctest::Approx(33.3));
    CHECK(summary.min_ms == doctest::Approx(16.0));
    CHECK(summary.acquire_ms == doctest::Approx(15));
    CHECK(summary.gpu_ms == doctest::Approx(4)); // Frames without GPU timing are skipped.
    CHECK(summary.deviation_ms > 1.0);
    CHECK(mj::game::describe(summary).find("100 frames") == 0);
    CHECK(mj::game::summarize({}).frames == 0);
}

TEST_CASE("frame pacing history feeds the overlay and writes a CSV log") {
    mj::game::FramePacing pacing;
    CHECK(pacing.stats().count == 0);
    const auto path = std::filesystem::path("frame-pacing-test.csv");
    REQUIRE(pacing.open_log(path));
    for (std::size_t i = 0; i < mj::game::FramePacing::k_history + 10; ++i) {
        FrameSample sample;
        sample.delta_ms = 16.667;
        sample.ticks = 2;
        sample.events = i == 3 ? "shadow_rebuilt_High" : "";
        pacing.add(sample);
    }
    const auto stats = pacing.stats();
    CHECK(stats.count == mj::game::FramePacing::k_history);
    CHECK(stats.mean_ms == doctest::Approx(16.667));
    CHECK(stats.deviation_ms < 1e-3);
    CHECK(stats.long_frames == 0);
    pacing.close_log();
    CHECK_FALSE(pacing.logging());
    std::ifstream file(path);
    std::string header, line;
    std::getline(file, header);
    CHECK(header.starts_with("frame,time_ms,delta_ms,sim_delta_ms,ticks"));
    std::size_t rows = 0;
    bool event = false;
    while (std::getline(file, line)) {
        ++rows;
        event = event || line.ends_with(",shadow_rebuilt_High");
    }
    CHECK(rows == mj::game::FramePacing::k_history + 10);
    CHECK(event);
    file.close();
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK_FALSE(pacing.open_log(std::filesystem::path("missing-directory") / "log.csv"));
}

TEST_CASE("frame pacing command line options are validated") {
    auto parse = [](std::initializer_list<std::string_view> args) {
        return mj::parse_launch_options({args.begin(), args.size()});
    };
    CHECK(parse({"--frame-log", "frames.csv"})->frame_log == "frames.csv");
    CHECK(parse({"--pacing-test"})->pacing_test);
    CHECK_FALSE(parse({"--frame-log"}));
    CHECK_FALSE(parse({"--frame-log", "a.csv", "--frame-log", "b.csv"}));
}

TEST_CASE("display pacing waits for the newest present the queue depth allows") {
    using mj::display_wait_target;
    CHECK_FALSE(
        display_wait_target(0, {.queued_frames = 1})); // Nothing presented on this swapchain yet.
    CHECK(display_wait_target(1, {.queued_frames = 1}) == 1U);
    CHECK(display_wait_target(40, {.queued_frames = 1}) ==
          40U); // The previous frame must be on screen.
    CHECK_FALSE(display_wait_target(1, {.queued_frames = 2}));
    CHECK(display_wait_target(2, {.queued_frames = 2}) == 1U);
    CHECK(display_wait_target(40, {.queued_frames = 2}) ==
          39U); // One finished frame may still be queued.
    CHECK(display_wait_target(40, {.queued_frames = 0}) ==
          40U); // Out-of-range depths are clamped to 1..2.
    CHECK(display_wait_target(40, {.queued_frames = 9}) == 39U);
    static_assert(mj::DisplayPacing{}.enabled && mj::DisplayPacing{}.queued_frames == 2);
}

TEST_CASE("presentation caps leave simulation time independent of the display") {
    for (const int fps : {30, 60, 120}) {
        mj::FixedStep fixed;
        unsigned ticks = 0;
        for (int frame = 0; frame < fps * 2; ++frame)
            fixed.advance(mj::game::frame_interval(fps), [&](float) { ++ticks; });
        CHECK(ticks == 240);
    }
    CHECK(mj::game::frame_interval(0) == 0);
    CHECK(mj::game::frame_interval(-1) == 0);
}
