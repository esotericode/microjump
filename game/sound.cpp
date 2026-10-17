#include "sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace mj::game {
namespace {
using audio::Tone;
using audio::Wave;

audio::Sound bake(std::initializer_list<Tone> tones, std::uint32_t seed = 1) {
    return audio::synthesize(std::span(tones.begin(), tones.size()), seed);
}

// A small bell: a sine with an inharmonic overtone, the way struck metal rings.
std::array<Tone, 3> bell(float hz, float delay, float gain, float ring = 0.8F) {
    return {Tone{.start_hz = hz,
                 .end_hz = hz,
                 .delay = delay,
                 .attack = 0.002F,
                 .release = ring,
                 .gain = gain},
            Tone{.start_hz = hz * 2.76F,
                 .end_hz = hz * 2.76F,
                 .delay = delay,
                 .attack = 0.001F,
                 .release = ring * 0.35F,
                 .gain = gain * 0.35F},
            Tone{.wave = Wave::triangle,
                 .start_hz = hz * 2,
                 .end_hz = hz * 2,
                 .delay = delay,
                 .attack = 0.002F,
                 .release = ring * 0.2F,
                 .gain = gain * 0.25F}};
}

audio::Sound bells(std::initializer_list<std::array<float, 3>> notes, float ring) {
    std::vector<Tone> tones;
    for (const auto &[hz, delay, gain] : notes) {
        const auto layers = bell(hz, delay, gain, ring);
        tones.insert(tones.end(), layers.begin(), layers.end());
    }
    return audio::synthesize(tones);
}

// A music box waltz in C, three bars round and round, with its last notes ringing into the
// first so the loop has no seam.
audio::Sound music_box() {
    constexpr float beat = 0.36F;
    // Semitones above C5 for each beat of the tune; the bass sounds on each bar's first beat.
    constexpr std::array<int, 24> tune{0, 4,  7,  12, 7,  4,  5,  9, 12, 17, 12, 9,
                                       7, 11, 14, 19, 14, 11, 12, 7, 4,  0,  4,  7};
    constexpr std::array<int, 8> bass{-12, -7, -12, -7, -5, -10, -12, -5};
    std::vector<Tone> tones;
    const auto pitch = [](int semitones) {
        return 523.25F * std::exp2(static_cast<float>(semitones) / 12);
    };
    for (std::size_t i = 0; i < tune.size(); ++i) {
        const auto layers = bell(pitch(tune[i]), static_cast<float>(i) * beat, 0.16F, 1.1F);
        tones.insert(tones.end(), layers.begin(), layers.end());
    }
    for (std::size_t i = 0; i < bass.size(); ++i) {
        const auto layers = bell(pitch(bass[i]), static_cast<float>(i) * beat * 3, 0.12F, 1.4F);
        tones.insert(tones.end(), layers.begin(), layers.end());
    }
    auto sound = audio::synthesize(tones);
    // The tune lasts 24 beats; everything after that rings on into the loop's start.
    const float tail = static_cast<float>(sound.samples.size()) / audio::k_sample_rate -
                       static_cast<float>(tune.size()) * beat;
    return audio::make_loop(std::move(sound), tail);
}

constexpr float k_step_length = 1.15F;   // Metres of ground per footstep at a run.
constexpr float k_tick_angle = k_pi / 6; // A gear ticks every twelfth of a turn.
} // namespace

const char *to_string(Cue cue) {
    constexpr std::array<const char *, static_cast<std::size_t>(Cue::count)> names{
        "step",      "jump",      "double-jump", "triple-jump",   "long-jump",   "wall-kick",
        "dive",      "rollout",   "land",        "land-hard",     "belly-slide", "skid",
        "bounce",    "clean-hop", "spark",       "hunt-complete", "checkpoint",  "gate",
        "gear-tick", "clunk",     "wind",        "music"};
    const auto index = static_cast<std::size_t>(cue);
    return index < names.size() ? names[index] : "unknown";
}

std::vector<audio::Sound> make_sound_bank() {
    std::vector<audio::Sound> bank(static_cast<std::size_t>(Cue::count));
    const auto set = [&](Cue cue, audio::Sound sound) {
        bank[static_cast<std::size_t>(cue)] = std::move(sound);
    };
    set(Cue::step,
        bake({{.wave = Wave::noise,
               .attack = 0.001F,
               .release = 0.05F,
               .gain = 0.5F,
               .lowpass_start_hz = 1600,
               .lowpass_end_hz = 600},
              {.start_hz = 150, .end_hz = 70, .attack = 0.002F, .release = 0.06F, .gain = 0.45F}}));
    set(Cue::jump, bake({{.start_hz = 330,
                          .end_hz = 700,
                          .attack = 0.005F,
                          .hold = 0.02F,
                          .release = 0.12F,
                          .gain = 0.42F},
                         {.wave = Wave::triangle,
                          .start_hz = 660,
                          .end_hz = 1400,
                          .release = 0.1F,
                          .gain = 0.12F},
                         {.wave = Wave::noise,
                          .release = 0.08F,
                          .gain = 0.12F,
                          .lowpass_start_hz = 2500,
                          .lowpass_end_hz = 800}}));
    set(Cue::double_jump, bake({{.start_hz = 392,
                                 .end_hz = 880,
                                 .attack = 0.005F,
                                 .hold = 0.03F,
                                 .release = 0.15F,
                                 .gain = 0.42F},
                                {.wave = Wave::triangle,
                                 .start_hz = 784,
                                 .end_hz = 1760,
                                 .release = 0.13F,
                                 .gain = 0.14F},
                                {.wave = Wave::noise,
                                 .release = 0.1F,
                                 .gain = 0.14F,
                                 .lowpass_start_hz = 3000,
                                 .lowpass_end_hz = 900}}));
    set(Cue::triple_jump,
        bake(
            {{.start_hz = 440,
              .end_hz = 1100,
              .attack = 0.006F,
              .hold = 0.05F,
              .release = 0.25F,
              .gain = 0.4F,
              .vibrato_hz = 7,
              .vibrato_semitones = 0.3F},
             {.wave = Wave::triangle,
              .start_hz = 880,
              .end_hz = 2200,
              .delay = 0.03F,
              .release = 0.2F,
              .gain = 0.13F},
             {.wave = Wave::noise,
              .attack = 0.02F,
              .release = 0.35F,
              .gain = 0.18F,
              .lowpass_start_hz = 3000,
              .lowpass_end_hz = 600},
             {.start_hz = 2093, .end_hz = 2093, .delay = 0.08F, .release = 0.2F, .gain = 0.08F},
             {.start_hz = 2637, .end_hz = 2637, .delay = 0.14F, .release = 0.25F, .gain = 0.08F}}));
    set(Cue::long_jump, bake({{.wave = Wave::noise,
                               .attack = 0.03F,
                               .hold = 0.05F,
                               .release = 0.3F,
                               .gain = 0.35F,
                               .lowpass_start_hz = 600,
                               .lowpass_end_hz = 2200},
                              {.start_hz = 260, .end_hz = 440, .release = 0.2F, .gain = 0.3F}},
                             7));
    set(Cue::wall_kick, bake({{.wave = Wave::square,
                               .start_hz = 240,
                               .end_hz = 360,
                               .attack = 0.002F,
                               .release = 0.07F,
                               .gain = 0.22F,
                               .lowpass_start_hz = 2200},
                              {.start_hz = 500, .end_hz = 900, .release = 0.1F, .gain = 0.25F},
                              {.wave = Wave::noise,
                               .attack = 0.001F,
                               .release = 0.03F,
                               .gain = 0.3F,
                               .lowpass_start_hz = 4000}},
                             11));
    set(Cue::dive, bake({{.wave = Wave::noise,
                          .attack = 0.02F,
                          .hold = 0.05F,
                          .release = 0.25F,
                          .gain = 0.35F,
                          .lowpass_start_hz = 3500,
                          .lowpass_end_hz = 900},
                         {.start_hz = 600, .end_hz = 280, .release = 0.2F, .gain = 0.22F}},
                        13));
    set(Cue::rollout,
        bake({{.wave = Wave::triangle,
               .start_hz = 280,
               .end_hz = 520,
               .release = 0.1F,
               .gain = 0.35F},
              {.wave = Wave::noise, .release = 0.06F, .gain = 0.2F, .lowpass_start_hz = 1800}},
             17));
    set(Cue::land,
        bake({{.start_hz = 130, .end_hz = 55, .attack = 0.002F, .release = 0.09F, .gain = 0.6F},
              {.wave = Wave::noise,
               .attack = 0.001F,
               .release = 0.07F,
               .gain = 0.45F,
               .lowpass_start_hz = 1200,
               .lowpass_end_hz = 400}},
             19));
    set(Cue::land_hard,
        bake({{.start_hz = 110, .end_hz = 40, .attack = 0.002F, .release = 0.18F, .gain = 0.8F},
              {.wave = Wave::noise,
               .attack = 0.001F,
               .release = 0.16F,
               .gain = 0.55F,
               .lowpass_start_hz = 1500,
               .lowpass_end_hz = 300},
              {.wave = Wave::square,
               .start_hz = 70,
               .end_hz = 40,
               .release = 0.12F,
               .gain = 0.2F,
               .lowpass_start_hz = 400}},
             23));
    set(Cue::belly_slide, bake({{.wave = Wave::noise,
                                 .attack = 0.02F,
                                 .hold = 0.15F,
                                 .release = 0.35F,
                                 .gain = 0.35F,
                                 .lowpass_start_hz = 1400,
                                 .lowpass_end_hz = 700},
                                {.start_hz = 120, .end_hz = 60, .release = 0.1F, .gain = 0.4F}},
                               29));
    set(Cue::skid, bake({{.wave = Wave::noise,
                          .attack = 0.01F,
                          .hold = 0.12F,
                          .release = 0.15F,
                          .gain = 0.5F,
                          .lowpass_start_hz = 4500,
                          .lowpass_end_hz = 2000},
                         {.wave = Wave::saw,
                          .start_hz = 180,
                          .end_hz = 150,
                          .hold = 0.1F,
                          .release = 0.1F,
                          .gain = 0.16F,
                          .lowpass_start_hz = 900}},
                        31));
    set(Cue::bounce, bake({{.start_hz = 160,
                            .end_hz = 540,
                            .attack = 0.005F,
                            .hold = 0.05F,
                            .release = 0.35F,
                            .gain = 0.5F,
                            .vibrato_hz = 16,
                            .vibrato_semitones = 0.8F},
                           {.wave = Wave::triangle,
                            .start_hz = 320,
                            .end_hz = 1080,
                            .release = 0.25F,
                            .gain = 0.12F}}));
    set(Cue::clean_hop,
        bells({{1760, 0, 0.15F}, {2217, 0.03F, 0.14F}, {2637, 0.06F, 0.12F}, {3520, 0.09F, 0.1F}},
              0.2F));
    set(Cue::spark, bells({{1046.5F, 0, 0.42F}, {2093, 0.06F, 0.13F}}, 0.9F));
    set(Cue::hunt_complete, bells({{1046.5F, 0, 0.25F},
                                   {1318.5F, 0.12F, 0.25F},
                                   {1568, 0.24F, 0.25F},
                                   {2093, 0.36F, 0.3F},
                                   {1046.5F, 0.6F, 0.15F},
                                   {1568, 0.6F, 0.15F}},
                                  1.4F));
    set(Cue::checkpoint, bells({{880, 0, 0.25F}, {1318.5F, 0.1F, 0.25F}}, 0.4F));
    set(Cue::gate,
        bake({{.wave = Wave::noise,
               .attack = 0.25F,
               .hold = 0.1F,
               .release = 0.5F,
               .gain = 0.6F,
               .lowpass_start_hz = 300,
               .lowpass_end_hz = 4000},
              {.start_hz = 220,
               .end_hz = 880,
               .attack = 0.2F,
               .release = 0.5F,
               .gain = 0.32F,
               .vibrato_hz = 6,
               .vibrato_semitones = 0.4F},
              {.start_hz = 1760, .end_hz = 1760, .delay = 0.3F, .release = 0.4F, .gain = 0.1F},
              {.start_hz = 2637, .end_hz = 2637, .delay = 0.4F, .release = 0.4F, .gain = 0.09F}},
             37));
    set(Cue::gear_tick,
        bake(
            {{.wave = Wave::noise,
              .attack = 0.0005F,
              .release = 0.02F,
              .gain = 0.45F,
              .lowpass_start_hz = 5000},
             {.start_hz = 2600, .end_hz = 2600, .attack = 0.0005F, .release = 0.03F, .gain = 0.18F},
             {.start_hz = 1200, .end_hz = 1150, .attack = 0.001F, .release = 0.05F, .gain = 0.14F}},
            41));
    set(Cue::clunk,
        bake({{.start_hz = 90, .end_hz = 50, .attack = 0.002F, .release = 0.15F, .gain = 0.6F},
              {.wave = Wave::square,
               .start_hz = 120,
               .end_hz = 120,
               .release = 0.08F,
               .gain = 0.18F,
               .lowpass_start_hz = 600},
              {.start_hz = 620, .end_hz = 600, .attack = 0.001F, .release = 0.12F, .gain = 0.08F},
              {.wave = Wave::noise,
               .delay = 0.03F,
               .attack = 0.02F,
               .hold = 0.1F,
               .release = 0.3F,
               .gain = 0.1F,
               .lowpass_start_hz = 7000,
               .lowpass_end_hz = 2000}},
             43));
    set(Cue::wind, audio::make_loop(bake({{.wave = Wave::noise,
                                           .attack = 0.5F,
                                           .hold = 6,
                                           .release = 0.5F,
                                           .gain = 0.9F,
                                           .lowpass_start_hz = 380,
                                           .lowpass_end_hz = 520},
                                          {.wave = Wave::noise,
                                           .attack = 2,
                                           .hold = 2,
                                           .release = 3,
                                           .gain = 0.35F,
                                           .lowpass_start_hz = 1100,
                                           .lowpass_end_hz = 700}},
                                         47),
                                    1.0F));
    set(Cue::music, music_box());
    return bank;
}

void SoundDirector::reset(const Simulation &simulation) {
    m_stride = 0;
    m_found = simulation.collected_count();
    m_checkpoint = simulation.active_checkpoint();
    const auto count = simulation.level().movers.size();
    m_ticks.assign(count, 0);
    m_positions.assign(count, {});
    m_moving.assign(count, false);
    for (std::size_t i = 0; i < count; ++i) {
        const auto &motion = simulation.level().movers[i].motion;
        m_ticks[i] = static_cast<std::int64_t>(
            std::floor(std::abs(motion.spin) * simulation.seconds() / k_tick_angle));
        m_positions[i] = simulation.mover_pose(i, 1).position;
    }
    // Loops that were playing fade out; the next tick brings back what this level has.
    for (auto &loop : m_loops)
        loop.gain = 0;
}

void SoundDirector::tick(const PlayerState &before, const Simulation &simulation, float dt,
                         std::vector<CueEvent> &sounds) {
    const auto &after = simulation.player.current();
    const auto &tuning = simulation.player.tuning;
    const auto play = [&](Cue cue, float gain = 1, float pitch = 1) {
        sounds.push_back({cue, gain, pitch, std::nullopt, 30});
    };

    // Takeoffs: an upward kick in velocity, named by the move that made it.
    const bool launched = after.velocity.y > before.velocity.y + 2.5F && !after.grounded;
    const bool kicked =
        after.state == MoveState::wall_jump &&
        (before.state != MoveState::wall_jump ||
         length_squared(after.last_kick_normal - before.last_kick_normal) > 1.0e-4F);
    if (kicked) {
        play(Cue::wall_kick);
    } else if (after.state == MoveState::dive && before.state != MoveState::dive) {
        play(Cue::dive);
    } else if (launched) {
        switch (after.state) {
        case MoveState::jump:
            play(Cue::jump);
            break;
        case MoveState::double_jump:
            play(Cue::double_jump);
            break;
        case MoveState::triple_jump:
            play(Cue::triple_jump);
            break;
        case MoveState::long_jump:
            play(Cue::long_jump);
            break;
        case MoveState::rollout:
            play(Cue::rollout);
            break;
        case MoveState::bounce:
            play(Cue::bounce);
            break;
        default:
            break;
        }
        if (before.grounded && after.last_hop == HopTiming::clean)
            play(Cue::clean_hop);
    }

    // Landings, louder and heavier the harder they hit.
    if (after.landed) {
        const float impact = std::max(-before.velocity.y, 0.0F);
        if (after.state == MoveState::dive_slide)
            play(Cue::belly_slide);
        else if (impact > 16)
            play(Cue::land_hard, std::min(impact / 24, 1.0F));
        else if (impact > 1)
            play(Cue::land, 0.25F + std::min(impact / 14, 0.75F));
        m_stride = 0;
    }
    if (after.state == MoveState::skid && before.state != MoveState::skid)
        play(Cue::skid);

    // Footsteps follow the ground covered, so they keep time with the run cycle.
    const float speed = after.grounded ? after.ground_speed : 0.0F;
    if (after.grounded && after.state != MoveState::dive_slide && speed > 0.5F) {
        m_stride += speed * dt;
        const float length = k_step_length * std::max(speed / tuning.run_speed, 0.6F);
        if (m_stride >= length) {
            m_stride -= length;
            m_left = !m_left;
            play(Cue::step, 0.35F + 0.4F * std::min(speed / tuning.run_speed, 1.0F),
                 m_left ? 1.0F : 0.92F);
        }
    }

    // Sparks chime up a pentatonic scale as the hunt goes on; the last one rings out.
    if (simulation.collected_count() > m_found) {
        constexpr std::array<int, 8> scale{0, 2, 4, 7, 9, 12, 14, 16};
        const auto step = scale[(simulation.collected_count() - 1) % scale.size()];
        play(Cue::spark, 1, std::exp2(static_cast<float>(step) / 12));
        if (simulation.collected_count() == simulation.collectible_count())
            play(Cue::hunt_complete);
    }
    m_found = simulation.collected_count();
    if (simulation.active_checkpoint() != m_checkpoint)
        play(Cue::checkpoint, 0.8F);
    m_checkpoint = simulation.active_checkpoint();

    // The machines: gears tick as they turn, lifts and pistons clunk where they stop.
    const auto &movers = simulation.level().movers;
    m_loops.clear();
    float music = 0;
    std::optional<Vec3> music_at;
    for (std::size_t i = 0; i < movers.size() && i < m_ticks.size(); ++i) {
        const Vec3 at = simulation.mover_pose(i, 1).position;
        switch (movers[i].sound) {
        case MoverSound::tick: {
            const auto ticks = static_cast<std::int64_t>(
                std::floor(std::abs(movers[i].motion.spin) * simulation.seconds() / k_tick_angle));
            if (ticks != m_ticks[i])
                sounds.push_back({Cue::gear_tick, 0.7F, 1, at, 24});
            m_ticks[i] = ticks;
            break;
        }
        case MoverSound::clunk: {
            const bool moving = length_squared(at - m_positions[i]) > 1.0e-8F;
            if (m_moving[i] != moving)
                sounds.push_back({Cue::clunk, moving ? 0.35F : 0.8F, moving ? 1.3F : 1.0F, at, 28});
            m_moving[i] = moving;
            break;
        }
        case MoverSound::music:
            music = 0.8F;
            music_at = at;
            break;
        case MoverSound::none:
            break;
        }
        m_positions[i] = at;
    }
    m_loops.push_back({Cue::music, music, 1, music_at, 34});

    // Wind rises with speed, and with a long fall.
    const float rush = std::max(length(horizontal(after.velocity)) / tuning.top_speed(),
                                std::clamp(-after.velocity.y / tuning.terminal_speed, 0.0F, 1.0F));
    const float wind = 0.08F + 0.6F * rush * rush;
    m_wind += (wind - m_wind) * std::min(dt * 4, 1.0F);
    m_loops.push_back({Cue::wind, m_wind, 0.85F + 0.4F * rush, std::nullopt, 30});
}

} // namespace mj::game
