#include "test_framework.hpp"

#include <level_io.hpp>
#include <sound.hpp>

#include <mj/audio/audio_output.hpp>
#include <mj/audio/mixer.hpp>
#include <mj/audio/synth.hpp>
#include <mj/core/launch_options.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>

using mj::Vec3;
namespace audio = mj::audio;
using mj::game::Cue;

namespace {
constexpr float k_dt = 1.0F / 120;

float peak(std::span<const float> samples) {
    float most = 0;
    for (const float s : samples)
        most = std::max(most, std::abs(s));
    return most;
}

// Largest jump between neighbouring samples: a click shows up as a big one.
float roughest(std::span<const float> samples, std::size_t stride = 1) {
    float most = 0;
    for (std::size_t i = stride; i < samples.size(); i += stride)
        most = std::max(most, std::abs(samples[i] - samples[i - stride]));
    return most;
}

audio::Sound tone(float hz, float seconds, float gain = 0.5F) {
    const std::array tones{audio::Tone{.start_hz = hz,
                                       .end_hz = hz,
                                       .attack = 0.01F,
                                       .hold = seconds,
                                       .release = 0.01F,
                                       .gain = gain}};
    return audio::synthesize(tones);
}

std::vector<float> mixed(audio::Mixer &mixer, std::size_t frames) {
    std::vector<float> out(frames * 2);
    for (std::size_t done = 0; done < frames; done += 512) {
        const std::size_t count = std::min<std::size_t>(512, frames - done);
        mixer.mix(std::span(out.data() + done * 2, count * 2));
    }
    return out;
}

float channel_peak(const std::vector<float> &stereo, std::size_t channel) {
    float most = 0;
    for (std::size_t i = channel; i < stereo.size(); i += 2)
        most = std::max(most, std::abs(stereo[i]));
    return most;
}

// 16-bit mono WAV, so a person can listen to what the tests baked.
void write_wav(const std::filesystem::path &path, const audio::Sound &sound) {
    std::ofstream file(path, std::ios::binary);
    const auto put = [&](auto value) {
        file.write(reinterpret_cast<const char *>(&value), sizeof value);
    };
    const auto bytes = static_cast<std::uint32_t>(sound.samples.size() * 2);
    file.write("RIFF", 4);
    put(std::uint32_t{36} + bytes);
    file.write("WAVEfmt ", 8);
    put(std::uint32_t{16});
    put(std::uint16_t{1});
    put(std::uint16_t{1});
    put(std::uint32_t{audio::k_sample_rate});
    put(std::uint32_t{audio::k_sample_rate * 2});
    put(std::uint16_t{2});
    put(std::uint16_t{16});
    file.write("data", 4);
    put(bytes);
    for (const float s : sound.samples)
        put(static_cast<std::int16_t>(std::clamp(s, -1.0F, 1.0F) * 32767));
}
} // namespace

TEST_CASE("the synthesizer bakes tones that start and end in silence") {
    const auto sound = tone(441, 0.5F);
    REQUIRE(sound.samples.size() > audio::k_sample_rate / 2);
    CHECK(std::abs(sound.samples.front()) < 1e-3F);
    CHECK(std::abs(sound.samples.back()) < 1e-3F);
    CHECK(peak(sound.samples) == doctest::Approx(0.5F).epsilon(0.02));
    // 441 Hz crosses zero upward 441 times a second.
    int crossings = 0;
    const auto middle =
        std::span(sound.samples).subspan(audio::k_sample_rate / 10, audio::k_sample_rate / 4);
    for (std::size_t i = 1; i < middle.size(); ++i)
        crossings += middle[i - 1] < 0 && middle[i] >= 0 ? 1 : 0;
    CHECK(crossings == doctest::Approx(441 / 4.0).epsilon(0.02));

    const std::array noisy{
        audio::Tone{.wave = audio::Wave::noise, .release = 0.2F, .lowpass_start_hz = 800}};
    const auto a = audio::synthesize(noisy, 5);
    const auto b = audio::synthesize(noisy, 5);
    const auto c = audio::synthesize(noisy, 6);
    CHECK(a.samples == b.samples); // Recipes bake the same sound every time.
    CHECK(a.samples != c.samples);
    CHECK(
        std::all_of(a.samples.begin(), a.samples.end(), [](float s) { return std::isfinite(s); }));
}

TEST_CASE("a loop's end flows into its start") {
    const std::array hum{audio::Tone{.wave = audio::Wave::noise,
                                     .attack = 0.2F,
                                     .hold = 2,
                                     .release = 0.2F,
                                     .lowpass_start_hz = 400}};
    const auto raw = audio::synthesize(hum, 3);
    const auto loop = audio::make_loop(raw, 0.5F);
    CHECK(loop.loop);
    CHECK(loop.samples.size() == raw.samples.size() - audio::k_sample_rate / 2);
    // Across the seam the signal moves no more than it does anywhere else.
    const float seam = std::abs(loop.samples.front() - loop.samples.back());
    CHECK(seam <= roughest(loop.samples) * 1.5F);
}

TEST_CASE("the mixer pans, fades with distance and limits its output") {
    audio::Mixer mixer({tone(440, 0.3F)});
    CHECK(peak(mixed(mixer, 4800)) == 0.0F); // Nothing playing: silence.

    mixer.play({.sound = 0});
    auto out = mixed(mixer, 4800);
    CHECK(channel_peak(out, 0) == doctest::Approx(channel_peak(out, 1)).epsilon(0.01));
    CHECK(channel_peak(out, 0) > 0.3F);

    mixed(mixer, 24000); // Let it finish.
    mixer.set_listener({{0, 0, 0}, {1, 0, 0}});
    mixer.play({.sound = 0, .position = Vec3{5, 0, 0}, .reach = 30});
    out = mixed(mixer, 24000);
    CHECK(channel_peak(out, 1) > channel_peak(out, 0) * 3); // Off to the right.
    mixer.play({.sound = 0, .position = Vec3{40, 0, 0}, .reach = 30});
    CHECK(peak(mixed(mixer, 24000)) == 0.0F); // Beyond its reach.

    // A stack of loud sounds bends under full scale instead of clipping.
    for (int i = 0; i < 20; ++i)
        mixer.play({.sound = 0, .gain = 1});
    out = mixed(mixer, 4800);
    CHECK(peak(out) <= 1.0F);
    CHECK(peak(out) > 0.9F);

    mixer.set_volume(0);
    mixer.play({.sound = 0});
    CHECK(peak(mixed(mixer, 4800)) == 0.0F);
}

TEST_CASE("the mixer plays at pitch, steals the oldest voice and eases its loops") {
    audio::Mixer mixer({tone(440, 0.2F), audio::make_loop(tone(220, 1.0F), 0.1F)});
    mixer.play({.sound = 0, .pitch = 2});
    auto out = mixed(mixer, 12000);
    // Twice the rate: over in half the time.
    std::size_t last = 0;
    for (std::size_t i = 0; i < out.size(); i += 2)
        last = std::abs(out[i]) > 1e-4F ? i / 2 : last;
    CHECK(last == doctest::Approx(0.11 * audio::k_sample_rate).epsilon(0.1));

    for (std::size_t i = 0; i < audio::Mixer::k_voices + 20; ++i)
        mixer.play({.sound = 0, .gain = 0.01F});
    mixed(mixer, 512);
    CHECK(mixer.active_voices() == audio::Mixer::k_voices);
    mixed(mixer, 24000);
    CHECK(mixer.active_voices() == 0);

    mixer.set_loop({.sound = 1, .gain = 0.8F});
    out = mixed(mixer, 48000);
    CHECK(mixer.active_voices() == 1);
    CHECK(roughest(out, 2) < 0.08F); // Eased in, and round the seam, without a click.
    mixer.set_loop({.sound = 1, .gain = 0});
    out = mixed(mixer, 12000);
    CHECK(roughest(out, 2) < 0.08F);
    CHECK(mixer.active_voices() == 0);
}

TEST_CASE("a pitched loop can wrap more than once between output samples") {
    const audio::Sound short_loop{{0.2F, -0.4F, 0.6F}, true};
    audio::Mixer fast({short_loop});
    audio::Mixer equivalent({short_loop});
    fast.set_loop({.sound = 0, .pitch = 8});
    equivalent.set_loop({.sound = 0, .pitch = 2});
    // Eight samples forward in a three-sample loop is the same as two. Both
    // callbacks must stay inside the loop, including across consecutive mixes.
    for (int i = 0; i < 3; ++i) {
        const auto out = mixed(fast, 64);
        CHECK(out == mixed(equivalent, 64));
        CHECK(std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s); }));
    }
    CHECK(fast.active_voices() == 1);

    audio::Mixer shortest({audio::Sound{{0.2F, -0.4F}, true}});
    shortest.set_loop({.sound = 0, .pitch = 8});
    const auto out = mixed(shortest, 64);
    CHECK(peak(out) > 0.0F);
    CHECK(std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s); }));
    CHECK(shortest.active_voices() == 1);
}

TEST_CASE("loop pitch stays safe both when starting and when changing a voice") {
    const auto sound = audio::make_loop(tone(220, 0.1F), 0.01F);
    const std::array pitches{std::array{-1.0F, 0.1F}, std::array{100.0F, 8.0F},
                             std::array{std::numeric_limits<float>::quiet_NaN(), 1.0F},
                             std::array{std::numeric_limits<float>::infinity(), 1.0F},
                             std::array{-std::numeric_limits<float>::infinity(), 1.0F}};
    for (const auto &pitch : pitches) {
        CAPTURE(pitch[0]);
        for (const bool existing : {false, true}) {
            CAPTURE(existing);
            audio::Mixer mixer({sound});
            audio::Mixer reference({sound});
            if (existing) {
                mixer.set_loop({.sound = 0});
                reference.set_loop({.sound = 0});
                mixed(mixer, 512);
                mixed(reference, 512);
            }
            mixer.set_loop({.sound = 0, .pitch = pitch[0]});
            reference.set_loop({.sound = 0, .pitch = pitch[1]});
            const auto out = mixed(mixer, 2048);
            CHECK(out == mixed(reference, 2048));
            CHECK(std::all_of(out.begin(), out.end(), [](float s) { return std::isfinite(s); }));
            CHECK(mixer.active_voices() == 1);
        }
    }
}

TEST_CASE("every game sound bakes quiet at its ends and loud enough to hear") {
    const auto start = std::chrono::steady_clock::now();
    const auto bank = mj::game::make_sound_bank();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("Baked " << bank.size() << " sounds in " << seconds << " s");
    REQUIRE(bank.size() == static_cast<std::size_t>(Cue::count));
    const auto out = std::filesystem::path(MJ_TEST_OUTPUT) / "sounds";
    std::filesystem::create_directories(out);
    for (std::size_t i = 0; i < bank.size(); ++i) {
        CAPTURE(i);
        const auto &sound = bank[i];
        REQUIRE(sound.samples.size() > 100);
        CHECK(std::all_of(sound.samples.begin(), sound.samples.end(),
                          [](float s) { return std::isfinite(s); }));
        CHECK(peak(sound.samples) > 0.05F);
        CHECK(peak(sound.samples) < 1.2F);
        if (!sound.loop) {
            CHECK(std::abs(sound.samples.front()) < 0.01F);
            CHECK(std::abs(sound.samples.back()) < 0.01F);
        } else {
            CHECK(std::abs(sound.samples.front() - sound.samples.back()) < 0.05F);
        }
        write_wav(out / (std::string(mj::game::to_string(static_cast<Cue>(i))) + ".wav"), sound);
    }
}

namespace {
mj::game::Level level_of(std::string_view movers) {
    auto level = mj::game::parse_level(
        R"({"version": 1, "name": "Sound test", "kill_height": -40,
      "shapes": [{"kind":"box", "center":[0,-0.5,0], "size":[200,1,200], "color":"#808080"}],
      "checkpoints": [{"position":[0,0.01,0]}, {"position":[0,0.01,-12]}],
      "collectibles": [{"name":"A", "position":[0,1,-6]}, {"name":"B", "position":[0,1,-8]}],
      "movers": [)" +
        std::string(movers) + "]}");
    REQUIRE(level);
    return *level;
}

struct Listening {
    mj::game::Level level;
    mj::game::LevelGeometry geometry;
    mj::game::Simulation simulation;
    mj::game::SoundDirector director;
    std::vector<mj::game::CueEvent> heard;
    explicit Listening(mj::game::Level loaded)
        : level(std::move(loaded)), geometry(mj::game::build_geometry(level)),
          simulation(level, geometry.collision) {
        director.reset(simulation);
    }
    void hold(mj::TickInput input, int ticks) {
        for (int i = 0; i < ticks; ++i) {
            const auto before = simulation.player.current();
            simulation.tick(input, k_dt);
            director.tick(before, simulation, k_dt, heard);
        }
    }
    [[nodiscard]] std::size_t count(Cue cue) const {
        return static_cast<std::size_t>(std::count_if(
            heard.begin(), heard.end(), [&](const auto &event) { return event.cue == cue; }));
    }
};
namespace button = mj::input_button;
} // namespace

TEST_CASE("the sound director hears steps, jumps, landings, sparks and checkpoints") {
    Listening run(level_of(""));
    run.hold({}, 60); // Settle at spawn: a soft landing, nothing else.
    CHECK(run.count(Cue::step) == 0);
    run.heard.clear();

    run.hold({0, 1, 0, 0, 0}, 200); // Run north past both sparks to the second checkpoint.
    CHECK(run.count(Cue::step) >= 6);
    CHECK(run.count(Cue::spark) == 2);
    CHECK(run.count(Cue::hunt_complete) == 1);
    CHECK(run.count(Cue::checkpoint) == 1);
    std::vector<float> chimes;
    for (const auto &event : run.heard) {
        if (event.cue == Cue::spark)
            chimes.push_back(event.pitch);
    }
    REQUIRE(chimes.size() == 2);
    CHECK(chimes[1] > chimes[0]); // Each find chimes higher.

    run.heard.clear();
    run.hold({0, 1, 0, 0, button::jump_pressed | button::jump_held}, 1);
    run.hold({0, 1, 0, 0, button::jump_held}, 150);
    CHECK(run.count(Cue::jump) == 1);
    CHECK(run.count(Cue::land) == 1);

    // Again on landing: a double jump, then a triple.
    run.heard.clear();
    for (int i = 0; i < 400 && run.count(Cue::triple_jump) == 0; ++i)
        run.hold({0, 1, 0, 0,
                  run.simulation.player.current().grounded
                      ? button::jump_pressed | button::jump_held
                      : button::jump_held},
                 1);
    CHECK(run.count(Cue::double_jump) == 1);
    CHECK(run.count(Cue::triple_jump) == 1);

    // Reset after a gate or a restore: what already happened stays quiet.
    run.director.reset(run.simulation);
    run.heard.clear();
    run.hold({}, 10);
    CHECK(run.count(Cue::spark) == 0);
    CHECK(run.count(Cue::checkpoint) == 0);
}

TEST_CASE("machines sound where they are: gears tick, lifts clunk, music boxes play") {
    Listening run(level_of(
        R"({"name":"Gear", "pivot":[10,0.5,0], "spin":0.5,
            "shapes":[{"kind":"cylinder","center":[0,-0.25,0],"size":[4,0.5,4],"color":"#b08d57"}]},
           {"name":"Lift", "pivot":[-10,0.5,0], "path":[[0,0,0],[0,4,0]], "period":6, "pause":1,
            "shapes":[{"kind":"box","center":[0,-0.25,0],"size":[2,0.5,2],"color":"#b08d57"}]},
           {"name":"Box", "pivot":[0,0.5,20], "spin":0.2, "sound":"music",
            "shapes":[{"kind":"cylinder","center":[0,-0.25,0],"size":[4,0.5,4],"color":"#b08d57"}]})"));
    run.hold({}, 120 * 13 / 2); // Six and a half seconds.
    // A twelfth of a turn per tick: 0.5 rad/s for 6.5 s is about 6.2 ticks.
    CHECK(run.count(Cue::gear_tick) == 6);
    // The lift stops twice and starts twice in one period.
    CHECK(run.count(Cue::clunk) == 4);
    for (const auto &event : run.heard) {
        if (event.cue == Cue::gear_tick) {
            REQUIRE(event.position);
            CHECK(event.position->x == doctest::Approx(10));
        }
    }
    bool music = false;
    for (const auto &loop : run.director.loops())
        music = music || (loop.cue == Cue::music && loop.gain > 0 && loop.position);
    CHECK(music);
    CHECK(run.level.movers[0].sound == mj::game::MoverSound::tick);
    CHECK(run.level.movers[1].sound == mj::game::MoverSound::clunk);
}

TEST_CASE("only play makes sound: scripted checks and captures stay silent") {
    const auto parse = [](std::initializer_list<std::string_view> args) {
        auto options = mj::parse_launch_options(std::vector<std::string_view>(args));
        REQUIRE(options);
        return mj::uses_audio(*options);
    };
    CHECK(parse({}));
    CHECK(parse({"--replay", "a.mjr"}));
    CHECK_FALSE(parse({"--no-audio"}));
    CHECK_FALSE(parse({"--smoke-test"}));
    CHECK_FALSE(parse({"--capture-tick", "5", "--capture-out", "a.png"}));
    CHECK_FALSE(parse({"--frames", "10"}));
    CHECK_FALSE(parse({"--travel-test"}));
}

TEST_CASE("the audio output feeds the mixer from SDL's audio thread") {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    audio::Mixer mixer({tone(440, 0.05F)});
    auto output = audio::AudioOutput::open(mixer);
    REQUIRE(output);
    CHECK((*output)->driver() == "dummy");
    mixer.play({.sound = 0});
    // The dummy device consumes in real time; within half a second the voice has played out.
    bool played = false;
    for (int i = 0; i < 50 && !played; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        played = mixer.active_voices() > 0;
    }
    for (int i = 0; i < 50 && mixer.active_voices() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(played);
    CHECK(mixer.active_voices() == 0);
    output->reset();
}
