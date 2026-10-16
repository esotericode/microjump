#include <mj/render/pacing.hpp>

namespace mj {

bool paired_presentation(const PresentationWindow &window) {
    const auto count = window.intervals.size();
    if (count < 16 || window.repeats.size() != count)
        return false;
    double mean = 0;
    for (const double value : window.intervals)
        mean += value / static_cast<double>(count);
    if (!(mean > 0))
        return false;
    std::size_t repeats = 0, short_frames = 0, long_frames = 0;
    for (std::size_t i = 0; i < count; ++i) {
        repeats += window.repeats[i] ? 1U : 0U;
        short_frames += window.intervals[i] < mean * 0.3 ? 1U : 0U;
        long_frames += window.intervals[i] > mean * 1.6 ? 1U : 0U;
    }
    // A healthy FIFO cadence has neither repeats nor a bimodal split; require plenty of both.
    const std::size_t quarter = count * 3 / 8;
    return repeats >= quarter && short_frames >= count * 5 / 16 && long_frames >= count * 5 / 16;
}

} // namespace mj
