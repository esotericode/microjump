#include <mj/audio/audio_output.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>

namespace mj::audio {

struct AudioOutput::Impl {
    Mixer *mixer = nullptr;
    SDL_AudioStream *stream = nullptr;
    bool subsystem = false;
    std::array<float, 1024> buffer{}; // 512 stereo frames per mix, reused.

    // SDL asks for more data on its audio thread; mix it in fixed-size chunks.
    static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int /*total*/) {
        auto &self = *static_cast<Impl *>(user);
        auto wanted = static_cast<std::size_t>(std::max(additional, 0)) / sizeof(float);
        while (wanted > 0) {
            const std::size_t count = std::min(wanted - wanted % 2, self.buffer.size());
            if (count == 0)
                break;
            self.mixer->mix(std::span(self.buffer.data(), count));
            SDL_PutAudioStreamData(stream, self.buffer.data(),
                                   static_cast<int>(count * sizeof(float)));
            wanted -= count;
        }
    }
};

AudioOutput::AudioOutput() : m_impl(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    if (m_impl->stream != nullptr)
        SDL_DestroyAudioStream(m_impl->stream); // Stops the callback before returning.
    if (m_impl->subsystem)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::expected<std::unique_ptr<AudioOutput>, Error> AudioOutput::open(Mixer &mixer) {
    std::unique_ptr<AudioOutput> output(new AudioOutput());
    auto &impl = *output->m_impl;
    impl.mixer = &mixer;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return std::unexpected(Error{std::string("No audio: ") + SDL_GetError()});
    impl.subsystem = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, k_sample_rate};
    impl.stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::feed, &impl);
    if (impl.stream == nullptr)
        return std::unexpected(Error{std::string("No audio device: ") + SDL_GetError()});
    if (!SDL_ResumeAudioStreamDevice(impl.stream))
        return std::unexpected(Error{std::string("Audio would not start: ") + SDL_GetError()});
    const char *driver = SDL_GetCurrentAudioDriver();
    output->m_driver = driver != nullptr ? driver : "unknown";
    return output;
}

} // namespace mj::audio
