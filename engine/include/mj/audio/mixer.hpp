#pragma once

#include <mj/audio/synth.hpp>
#include <mj/math/math.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace mj::audio {

// Where sounds are heard from: the camera, with its right-hand direction for panning.
struct Listener {
    Vec3 position;
    Vec3 right{1, 0, 0};
};

struct Play {
    std::uint32_t sound = 0; // Index into the mixer's bank.
    float gain = 1;
    float pitch = 1; // Playback rate: 2 is an octave up and half as long.
    // Placed in the world, panned and faded with distance; otherwise heard dead centre.
    std::optional<Vec3> position = std::nullopt;
    float reach = 30; // Metres at which a placed sound falls silent.
};

// Mixes baked sounds into interleaved stereo. The game thread queues plays and loop
// changes; the audio thread calls mix(). Commands cross under a short lock, so mixing
// never waits on the game and the game never waits on mixing.
class Mixer {
  public:
    static constexpr std::size_t k_voices = 48;

    explicit Mixer(std::vector<Sound> bank);

    void play(const Play &play);
    // A looping sound's level, eased toward `gain` over about a tenth of a second.
    // Gain 0 lets it fade out. Its pitch and placement follow the latest call.
    void set_loop(const Play &loop);
    void set_listener(const Listener &listener);
    void set_volume(float volume); // Master level, 0..1.

    // Fills `stereo` (left, right, left, right...) with the next frames. Output is softly
    // limited to [-1, 1].
    void mix(std::span<float> stereo);
    // Voices sounding after the last mix, for diagnostics and tests.
    [[nodiscard]] std::size_t active_voices() const {
        return m_active.load(std::memory_order_relaxed);
    }

  private:
    struct Voice {
        std::uint32_t sound = 0;
        double cursor = 0; // Sample position, fractional for pitch.
        float pitch = 1;
        float gain = 0;   // Current level (loops ease toward target).
        float target = 0; // Loops only.
        bool loop = false;
        bool active = false;
        std::optional<Vec3> position;
        float reach = 30;
        std::uint64_t started = 0; // For stealing the oldest voice.
    };
    struct Pending {
        Play play;
        bool loop = false;
    };
    [[nodiscard]] std::array<float, 2> pan(const Voice &voice) const;
    void apply(const Pending &command);

    std::vector<Sound> m_bank;
    std::array<Voice, k_voices> m_voices{};
    std::uint64_t m_started = 0;
    Listener m_listener;
    float m_volume = 1;
    std::atomic<std::size_t> m_active{0};

    mutable std::mutex m_mutex; // Guards everything below.
    std::vector<Pending> m_pending;
    std::vector<Pending> m_taken; // Swapped with m_pending, so mixing never allocates.
    Listener m_next_listener;
    float m_next_volume = 1;
};

} // namespace mj::audio
