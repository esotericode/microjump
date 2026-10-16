#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace mj::game {

// Snaps a measured frame interval to a whole number of display refreshes when it is within
// `tolerance` seconds of one, removing timer jitter from a vsynced frame cadence. Other
// intervals (missed or unsynchronized frames) pass through unchanged.
struct RefreshSnap {
    double refresh_hz = 0; // 0 (unknown) leaves intervals unchanged.
    double tolerance = 0.002;
};
[[nodiscard]] double snap_to_refresh(double elapsed, const RefreshSnap &snap);

// One rendered frame, for the pacing graph and the optional CSV log. Times in milliseconds.
struct FrameSample {
    double delta_ms = 0;     // Measured interval since the previous frame.
    double sim_delta_ms = 0; // Interval fed to the fixed-step clock (after optional snapping).
    std::uint32_t ticks = 0; // Simulation ticks run this frame.
    float alpha = 0;         // Render interpolation factor.
    double sim_cpu_ms = 0;   // Time spent running those ticks.
    double rebuild_ms = 0;
    double wait_ms = 0;
    double acquire_ms = 0;
    double record_ms = 0;
    double present_ms = 0;
    double gpu_ms = -1;
    std::uint32_t image = 0;
    float mouse_dx = 0; // Relative mouse motion applied this frame (pixels).
    float mouse_dy = 0;
    std::string events; // Semicolon-separated, e.g. "shadow_rebuilt;tuning_open".
};

struct PacingStats {
    double mean_ms = 0;
    double deviation_ms = 0; // Standard deviation of frame intervals.
    double min_ms = 0;
    double max_ms = 0;
    std::uint32_t long_frames = 0;  // More than 1.5x the mean.
    std::uint32_t uneven_ticks = 0; // Frames whose tick count differs from the previous frame.
    std::size_t count = 0;
};

// Steady-state statistics for a run of frames (e.g. one --pacing-test phase).
struct PacingSummary {
    std::size_t frames = 0;
    double mean_ms = 0;
    double deviation_ms = 0;
    double min_ms = 0;
    double max_ms = 0;
    std::uint32_t long_frames = 0;  // More than 1.5x the median interval.
    std::uint32_t uneven_ticks = 0; // Tick count differs from the previous frame.
    double wait_ms = 0;             // Means of the renderer's blocking points.
    double acquire_ms = 0;
    double present_ms = 0;
    double gpu_ms = -1;
};
[[nodiscard]] PacingSummary summarize(std::span<const FrameSample> samples);
[[nodiscard]] std::string describe(const PacingSummary &summary);

class FramePacing {
  public:
    static constexpr std::size_t k_history = 240;

    void add(const FrameSample &sample);
    [[nodiscard]] PacingStats stats() const;
    // Oldest first, for ImGui::PlotLines with values_offset = offset().
    [[nodiscard]] const std::array<float, k_history> &intervals() const {
        return m_intervals;
    }
    [[nodiscard]] std::size_t offset() const {
        return m_next;
    }

    // CSV log of every frame; returns false (and stays closed) if the file cannot be opened.
    bool open_log(const std::filesystem::path &path);
    void close_log();
    [[nodiscard]] bool logging() const {
        return m_log.is_open();
    }
    [[nodiscard]] const std::filesystem::path &log_path() const {
        return m_log_path;
    }

  private:
    std::array<float, k_history> m_intervals{};
    std::array<std::uint32_t, k_history> m_ticks{};
    std::size_t m_next = 0;
    std::size_t m_count = 0;
    std::uint64_t m_frame = 0;
    double m_time_ms = 0;
    std::ofstream m_log;
    std::filesystem::path m_log_path;
};

} // namespace mj::game
