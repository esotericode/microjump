#include "test_framework.hpp"

#include <frame_pacing.hpp>

#include <mj/core/launch_options.hpp>
#include <mj/render/pacing.hpp>

#include <algorithm>
#include <array>
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

namespace {
// 32-frame windows from frame logs (docs/frame-pacing.md): an NVIDIA Windows driver before and
// after recreating the swapchain, and the Linux development machine.
struct LoggedWindow {
    std::vector<double> intervals;
    std::vector<bool> repeats;
};
std::vector<bool> alternating(bool first) {
    std::vector<bool> repeats;
    for (int i = 0; i < 32; ++i)
        repeats.push_back((i % 2 == 0) == first);
    return repeats;
}
} // namespace

TEST_CASE("paired presentation is recognised from logged frames and healthy cadences are not") {
    auto paired = [](const LoggedWindow &window) {
        std::array<bool, 32> repeats{};
        std::copy(window.repeats.begin(), window.repeats.end(), repeats.begin());
        return mj::paired_presentation({window.intervals, repeats});
    };
    const LoggedWindow windows_test{
        {1.68,  32.43, 2.04,  30.42, 39.09, 0.1,   12.16, 3.02,  15.45, 2.91, 29.75,
         3.09,  46.42, 3.22,  29.98, 3.1,   30.22, 3.04,  30.28, 3.04,  30.3, 3.1,
         30.33, 3.36,  29.96, 3.25,  30.16, 3.19,  30.3,  3.1,   30.05, 3.15},
        {true,  false, true,  true,  false, false, false, false, false, true,  false,
         true,  false, true,  false, true,  false, true,  false, true,  false, true,
         false, true,  false, true,  false, true,  false, true,  false, true}};
    const LoggedWindow windows_manual{{3.52, 29.97, 3.6,  29.5,  3.76, 29.72, 3.64, 29.86,
                                       3.73, 29.43, 3.72, 29.67, 3.73, 29.61, 3.8,  29.49,
                                       3.77, 29.56, 3.71, 29.84, 3.63, 29.39, 3.7,  29.69,
                                       3.69, 29.67, 3.72, 29.56, 3.76, 29.6,  3.59, 29.78},
                                      alternating(true)};
    const LoggedWindow windows_recovered{{16.8,  16.35, 17.7,  15.94, 16.72, 16.46, 16.79, 16.41,
                                          17.54, 16.08, 17.05, 16.54, 16.34, 16.57, 17.38, 16.14,
                                          16.64, 16.56, 16.46, 16.75, 17.57, 15.96, 16.69, 16.74,
                                          16.59, 16.46, 17.42, 16.16, 16.71, 16.56, 16.45, 16.68},
                                         std::vector<bool>(32, false)};
    const LoggedWindow windows_start{{14.83, 16.74, 16.5,  16.84, 16.54, 18.9,  14.65, 16.56,
                                      16.42, 16.68, 16.84, 19.46, 13.8,  16.89, 16.56, 16.79,
                                      16.43, 19.13, 14.2,  16.81, 16.41, 17.06, 16.35, 18.66,
                                      14.92, 16.62, 16.53, 16.82, 16.39, 18.88, 14.6,  16.68},
                                     std::vector<bool>(32, false)};
    CHECK(paired(windows_test));
    CHECK(paired(windows_manual));
    CHECK_FALSE(paired(windows_recovered));
    CHECK_FALSE(paired(windows_start));
    // Either symptom alone is not enough: image reuse with an even cadence (e.g. a copying
    // presenter that keeps pace), or a bimodal cadence without reuse (missed refreshes).
    auto even_reuse = windows_recovered;
    even_reuse.repeats = alternating(true);
    CHECK_FALSE(paired(even_reuse));
    auto bimodal_only = windows_manual;
    bimodal_only.repeats = std::vector<bool>(32, false);
    CHECK_FALSE(paired(bimodal_only));
    CHECK_FALSE(mj::paired_presentation({}));
}
