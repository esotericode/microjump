#include "frame_pacing.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace mj::game {

double frame_interval(int frames_per_second) {
    return frames_per_second == 30 || frames_per_second == 60 || frames_per_second == 120
               ? 1.0 / frames_per_second
               : 0;
}

double snap_to_refresh(double elapsed, const RefreshSnap &snap) {
    if (!std::isfinite(elapsed) || !(snap.refresh_hz > 0) || elapsed <= 0)
        return elapsed;
    const double period = 1.0 / snap.refresh_hz;
    const double refreshes = std::round(elapsed / period);
    if (refreshes < 1 || std::abs(elapsed - refreshes * period) > snap.tolerance)
        return elapsed;
    return refreshes * period;
}

PacingSummary summarize(std::span<const FrameSample> samples) {
    PacingSummary summary;
    summary.frames = samples.size();
    if (samples.empty())
        return summary;
    std::vector<double> intervals;
    intervals.reserve(samples.size());
    double sum = 0, gpu = 0;
    std::size_t gpu_frames = 0;
    summary.min_ms = samples.front().delta_ms;
    summary.max_ms = samples.front().delta_ms;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto &sample = samples[i];
        intervals.push_back(sample.delta_ms);
        sum += sample.delta_ms;
        summary.min_ms = std::min(summary.min_ms, sample.delta_ms);
        summary.max_ms = std::max(summary.max_ms, sample.delta_ms);
        summary.display_wait_ms += sample.display_wait_ms;
        summary.wait_ms += sample.wait_ms;
        summary.acquire_ms += sample.acquire_ms;
        summary.present_ms += sample.present_ms;
        if (sample.gpu_ms >= 0) {
            gpu += sample.gpu_ms;
            ++gpu_frames;
        }
        if (i > 0 && sample.ticks != samples[i - 1].ticks)
            ++summary.uneven_ticks;
    }
    const auto count = static_cast<double>(samples.size());
    summary.mean_ms = sum / count;
    summary.display_wait_ms /= count;
    summary.wait_ms /= count;
    summary.acquire_ms /= count;
    summary.present_ms /= count;
    summary.gpu_ms = gpu_frames > 0 ? gpu / static_cast<double>(gpu_frames) : -1;
    std::ranges::nth_element(intervals,
                             intervals.begin() + static_cast<std::ptrdiff_t>(intervals.size() / 2));
    const double median = intervals[intervals.size() / 2];
    double squares = 0;
    for (const auto &sample : samples) {
        squares += (sample.delta_ms - summary.mean_ms) * (sample.delta_ms - summary.mean_ms);
        if (sample.delta_ms > median * 1.5)
            ++summary.long_frames;
    }
    summary.deviation_ms = std::sqrt(squares / count);
    return summary;
}

std::string describe(const PacingSummary &summary) {
    return std::format("{} frames, {:.3f} ms mean, jitter {:.3f} ms, range {:.2f}-{:.2f} ms, "
                       "long {}, uneven ticks {}, display {:.2f} / wait {:.2f} / acquire {:.2f} / "
                       "present {:.2f} ms, GPU {:.2f} ms",
                       summary.frames, summary.mean_ms, summary.deviation_ms, summary.min_ms,
                       summary.max_ms, summary.long_frames, summary.uneven_ticks,
                       summary.display_wait_ms, summary.wait_ms, summary.acquire_ms,
                       summary.present_ms, summary.gpu_ms);
}

void FramePacing::add(const FrameSample &sample) {
    m_intervals[m_next] = static_cast<float>(sample.delta_ms);
    m_ticks[m_next] = sample.ticks;
    m_next = (m_next + 1) % k_history;
    m_count = std::min(m_count + 1, k_history);
    ++m_frame;
    m_time_ms += sample.delta_ms;
    if (!m_log.is_open())
        return;
    m_log << std::format("{},{:.3f},{:.3f},{:.3f},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},"
                         "{:.3f},{:.3f},{:.3f},{},{:.2f},{:.2f},{}\n",
                         m_frame, m_time_ms, sample.delta_ms, sample.sim_delta_ms, sample.ticks,
                         sample.alpha, sample.sim_cpu_ms, sample.display_wait_ms, sample.rebuild_ms,
                         sample.wait_ms, sample.acquire_ms, sample.record_ms, sample.present_ms,
                         sample.gpu_ms, sample.image, sample.mouse_dx, sample.mouse_dy,
                         sample.events);
}

PacingStats FramePacing::stats() const {
    PacingStats stats;
    stats.count = m_count;
    if (m_count == 0)
        return stats;
    // Oldest sample first.
    const std::size_t first = (m_next + k_history - m_count) % k_history;
    double sum = 0;
    stats.min_ms = m_intervals[first];
    stats.max_ms = m_intervals[first];
    for (std::size_t i = 0; i < m_count; ++i) {
        const double value = m_intervals[(first + i) % k_history];
        sum += value;
        stats.min_ms = std::min(stats.min_ms, value);
        stats.max_ms = std::max(stats.max_ms, value);
    }
    stats.mean_ms = sum / static_cast<double>(m_count);
    double squares = 0;
    for (std::size_t i = 0; i < m_count; ++i) {
        const std::size_t at = (first + i) % k_history;
        const double difference = m_intervals[at] - stats.mean_ms;
        squares += difference * difference;
        if (m_intervals[at] > stats.mean_ms * 1.5)
            ++stats.long_frames;
        if (i > 0 && m_ticks[at] != m_ticks[(at + k_history - 1) % k_history])
            ++stats.uneven_ticks;
    }
    stats.deviation_ms = std::sqrt(squares / static_cast<double>(m_count));
    return stats;
}

bool FramePacing::open_log(const std::filesystem::path &path) {
    close_log();
    m_log.open(path, std::ios::trunc);
    if (!m_log.is_open())
        return false;
    m_log_path = path;
    m_log << "frame,time_ms,delta_ms,sim_delta_ms,ticks,alpha,sim_cpu_ms,display_wait_ms,"
             "rebuild_ms,wait_ms,acquire_ms,record_ms,present_ms,gpu_ms,image,mouse_dx,mouse_dy,"
             "events\n";
    return true;
}

void FramePacing::close_log() {
    if (m_log.is_open())
        m_log.close();
}

} // namespace mj::game
