#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mj::audio {

inline constexpr int k_sample_rate = 48000;

enum class Wave : std::uint8_t { sine, triangle, square, saw, noise };

// One layer of a synthesized sound: an oscillator swept in pitch, shaped by an envelope and
// an optional low-pass filter that can sweep too. Times are seconds, pitches hertz.
struct Tone {
    Wave wave = Wave::sine;
    float start_hz = 440;
    float end_hz = 440; // Swept exponentially across the attack, hold and release.
    float delay = 0;    // Silence before the tone starts.
    float attack = 0.004F;
    float hold = 0;        // At full level after the attack.
    float release = 0.15F; // Then fades to exact silence on a curve.
    float gain = 1;
    float lowpass_start_hz = 0; // 0: unfiltered.
    float lowpass_end_hz = 0;   // 0: the start cutoff throughout.
    float vibrato_hz = 0;
    float vibrato_semitones = 0;
};

// Mono samples at k_sample_rate, starting and ending at silence.
struct Sound {
    std::vector<float> samples;
    bool loop = false; // Plays round and round; its end flows into its start.
};

// Sums the layers. Noise comes from `seed`, so a recipe always bakes the same sound.
[[nodiscard]] Sound synthesize(std::span<const Tone> tones, std::uint32_t seed = 1);

// Turns a sound into a seamless loop by crossfading its last `crossfade` seconds into its
// start. The result is that much shorter.
[[nodiscard]] Sound make_loop(Sound sound, float crossfade);

} // namespace mj::audio
