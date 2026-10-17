#pragma once

#include <mj/audio/mixer.hpp>
#include <mj/core/error.hpp>

#include <expected>
#include <memory>
#include <string>

namespace mj::audio {

// Plays a mixer through the system's default output device on SDL's audio thread.
// Opening can fail (no device, no server); callers then carry on in silence.
class AudioOutput {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<AudioOutput>, Error> open(Mixer &mixer);
    ~AudioOutput();
    AudioOutput(const AudioOutput &) = delete;
    AudioOutput &operator=(const AudioOutput &) = delete;

    [[nodiscard]] const std::string &driver() const {
        return m_driver;
    }

  private:
    struct Impl;
    AudioOutput();
    std::unique_ptr<Impl> m_impl;
    std::string m_driver;
};

} // namespace mj::audio
