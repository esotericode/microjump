#include <mj/audio/synth.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mj::audio {
namespace {
constexpr double k_tau = 2 * std::numbers::pi;

// xorshift32: cheap, deterministic noise in [-1, 1].
float noise(std::uint32_t &state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return static_cast<float>(state) / 2147483648.0F - 1.0F;
}

float oscillate(Wave wave, double phase, std::uint32_t &state) {
    switch (wave) {
    case Wave::sine:
        return static_cast<float>(std::sin(k_tau * phase));
    case Wave::triangle:
        return static_cast<float>(1 - 4 * std::abs(phase - 0.5));
    case Wave::square:
        return phase < 0.5 ? 1.0F : -1.0F;
    case Wave::saw:
        return static_cast<float>(2 * phase - 1);
    case Wave::noise:
        return noise(state);
    }
    return 0;
}

// Rises smoothly from silence, holds, then falls on a curve that reaches exact silence.
float envelope(const Tone &tone, double t) {
    if (t < tone.attack) {
        const double x = t / tone.attack;
        return static_cast<float>(x * x * (3 - 2 * x));
    }
    if (t < tone.attack + tone.hold)
        return 1;
    const double x = std::min((t - tone.attack - tone.hold) / tone.release, 1.0);
    return static_cast<float>((1 - x) * (1 - x));
}

void render(const Tone &input, std::vector<float> &out, std::uint32_t &state) {
    Tone tone = input;
    tone.attack = std::max(tone.attack, 0.001F); // Never start with a click.
    tone.release = std::max(tone.release, 0.001F);
    const double length = tone.attack + tone.hold + tone.release;
    const auto first = static_cast<std::size_t>(std::lround(tone.delay * k_sample_rate));
    const auto count = static_cast<std::size_t>(std::ceil(length * k_sample_rate)) + 1;
    if (out.size() < first + count)
        out.resize(first + count, 0.0F);
    const double ratio = tone.end_hz / tone.start_hz;
    const double cutoff_ratio =
        tone.lowpass_end_hz > 0 ? tone.lowpass_end_hz / tone.lowpass_start_hz : 1.0;
    double phase = 0;
    float low = 0;
    float lower = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / k_sample_rate;
        const double along = std::min(t / length, 1.0);
        double hz = tone.start_hz * std::pow(ratio, along);
        if (tone.vibrato_semitones != 0)
            hz *= std::exp2(tone.vibrato_semitones / 12.0 * std::sin(k_tau * tone.vibrato_hz * t));
        phase += hz / k_sample_rate;
        phase -= std::floor(phase);
        float sample = oscillate(tone.wave, phase, state);
        if (tone.lowpass_start_hz > 0) {
            // Two one-pole stages: a gentle 12 dB/octave roll-off.
            const double cutoff = tone.lowpass_start_hz * std::pow(cutoff_ratio, along);
            const auto alpha = static_cast<float>(
                1 - std::exp(-k_tau * std::min(cutoff, 20000.0) / k_sample_rate));
            low += alpha * (sample - low);
            lower += alpha * (low - lower);
            sample = lower;
        }
        out[first + i] += tone.gain * envelope(tone, t) * sample;
    }
}
} // namespace

Sound synthesize(std::span<const Tone> tones, std::uint32_t seed) {
    Sound sound;
    std::uint32_t state = seed == 0 ? 1 : seed;
    for (const auto &tone : tones)
        render(tone, sound.samples, state);
    return sound;
}

Sound make_loop(Sound sound, float crossfade) {
    auto &s = sound.samples;
    const auto fade = std::min(static_cast<std::size_t>(crossfade * k_sample_rate), s.size() / 2);
    const std::size_t length = s.size() - fade;
    for (std::size_t i = 0; i < fade; ++i) {
        // Equal-power: the start fades in exactly as the tail it replaces fades out.
        const double w = (static_cast<double>(i) + 0.5) / static_cast<double>(fade);
        s[i] = static_cast<float>(s[i] * std::sin(w * std::numbers::pi / 2) +
                                  s[length + i] * std::cos(w * std::numbers::pi / 2));
    }
    s.resize(length);
    sound.loop = true;
    return sound;
}

} // namespace mj::audio
