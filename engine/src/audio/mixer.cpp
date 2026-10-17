#include <mj/audio/mixer.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mj::audio {
namespace {
// Loops ease to a new level over this long, so changes never click.
constexpr float k_loop_ease_seconds = 0.1F;

float playback_pitch(float pitch) {
    return std::isfinite(pitch) ? std::clamp(pitch, 0.1F, 8.0F) : 1.0F;
}

// Leaves quiet signals alone and bends peaks smoothly under full scale.
float limit(float x) {
    constexpr float knee = 0.8F;
    const float size = std::abs(x);
    if (size <= knee)
        return x;
    const float bent = knee + (1 - knee) * std::tanh((size - knee) / (1 - knee));
    return x < 0 ? -bent : bent;
}
} // namespace

Mixer::Mixer(std::vector<Sound> bank) : m_bank(std::move(bank)) {
    m_pending.reserve(64);
    m_taken.reserve(64);
}

void Mixer::play(const Play &play) {
    const std::lock_guard lock(m_mutex);
    m_pending.push_back({play, false});
}

void Mixer::set_loop(const Play &loop) {
    const std::lock_guard lock(m_mutex);
    m_pending.push_back({loop, true});
}

void Mixer::set_listener(const Listener &listener) {
    const std::lock_guard lock(m_mutex);
    m_next_listener = listener;
}

void Mixer::set_volume(float volume) {
    const std::lock_guard lock(m_mutex);
    m_next_volume = std::clamp(volume, 0.0F, 1.0F);
}

void Mixer::apply(const Pending &command) {
    const auto &play = command.play;
    if (play.sound >= m_bank.size() || m_bank[play.sound].samples.size() < 2)
        return;
    if (command.loop) {
        for (auto &voice : m_voices) {
            if (voice.active && voice.loop && voice.sound == play.sound) {
                voice.target = std::max(play.gain, 0.0F);
                voice.pitch = playback_pitch(play.pitch);
                voice.position = play.position;
                voice.reach = play.reach;
                return;
            }
        }
        if (play.gain <= 0)
            return;
    }
    // A free voice, or else the oldest one-shot: a new sound matters more than a fading one.
    Voice *slot = nullptr;
    for (auto &voice : m_voices) {
        if (!voice.active) {
            slot = &voice;
            break;
        }
        if (!voice.loop && (slot == nullptr || voice.started < slot->started))
            slot = &voice;
    }
    if (slot == nullptr)
        return;
    *slot = Voice{.sound = play.sound,
                  .pitch = playback_pitch(play.pitch),
                  .gain = command.loop ? 0.0F : play.gain,
                  .target = play.gain,
                  .loop = command.loop,
                  .active = true,
                  .position = play.position,
                  .reach = play.reach,
                  .started = ++m_started};
}

std::array<float, 2> Mixer::pan(const Voice &voice) const {
    constexpr float centre = std::numbers::sqrt2_v<float> / 2;
    if (!voice.position)
        return {centre, centre};
    const Vec3 offset = *voice.position - m_listener.position;
    const float distance = length(offset);
    const float near = std::clamp(1 - distance / std::max(voice.reach, 0.1F), 0.0F, 1.0F);
    const float fade = near * near;
    // Constant power: equal loudness wherever the sound sits between the ears.
    const float side =
        distance > 1e-3F ? std::clamp(dot(offset, m_listener.right) / distance, -1.0F, 1.0F) : 0.0F;
    const float angle = (side + 1) * std::numbers::pi_v<float> / 4;
    return {std::cos(angle) * fade, std::sin(angle) * fade};
}

void Mixer::mix(std::span<float> stereo) {
    {
        const std::lock_guard lock(m_mutex);
        std::swap(m_pending, m_taken);
        m_listener = m_next_listener;
        m_volume = m_next_volume;
    }
    for (const auto &command : m_taken)
        apply(command);
    m_taken.clear();

    std::fill(stereo.begin(), stereo.end(), 0.0F);
    const std::size_t frames = stereo.size() / 2;
    if (frames == 0)
        return;
    const float ease = static_cast<float>(frames) / (k_loop_ease_seconds * k_sample_rate);
    std::size_t active = 0;
    for (auto &voice : m_voices) {
        if (!voice.active)
            continue;
        const auto &samples = m_bank[voice.sound].samples;
        const auto size = static_cast<double>(samples.size());
        const float from = voice.gain;
        if (voice.loop)
            voice.gain = approach(voice.gain, voice.target, ease);
        const auto [left, right] = pan(voice);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto index = static_cast<std::size_t>(voice.cursor);
            const auto frac = static_cast<float>(voice.cursor - static_cast<double>(index));
            const std::size_t next =
                index + 1 < samples.size() ? index + 1 : (voice.loop ? 0 : index);
            const float sample = samples[index] + (samples[next] - samples[index]) * frac;
            const float gain = from + (voice.gain - from) * (static_cast<float>(frame) + 1) /
                                          static_cast<float>(frames);
            stereo[frame * 2] += sample * gain * left;
            stereo[frame * 2 + 1] += sample * gain * right;
            voice.cursor += voice.pitch;
            if (voice.cursor >= (voice.loop ? size : size - 1)) {
                if (!voice.loop) {
                    voice.active = false;
                    break;
                }
                // A short loop can wrap more than once at a high playback rate.
                voice.cursor = std::fmod(voice.cursor, size);
            }
        }
        if (voice.loop && voice.gain <= 0 && voice.target <= 0)
            voice.active = false;
        active += voice.active ? 1 : 0;
    }
    for (auto &sample : stereo)
        sample = limit(sample * m_volume);
    m_active.store(active, std::memory_order_relaxed);
}

} // namespace mj::audio
