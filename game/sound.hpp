#pragma once

#include "simulation.hpp"

#include <mj/audio/synth.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace mj::game {

// Every sound the game makes; each indexes the bank from make_sound_bank().
enum class Cue : std::uint8_t {
    step,
    jump,
    double_jump,
    triple_jump,
    long_jump,
    wall_kick,
    dive,
    rollout,
    land,
    land_hard,
    belly_slide,
    skid,
    bounce,
    clean_hop,
    spark,
    hunt_complete,
    checkpoint,
    gate,
    gear_tick,
    clunk,
    wind,  // Loop.
    music, // Loop: a carousel's music box.
    count,
};

[[nodiscard]] const char *to_string(Cue cue);

// Bakes every cue from synthesizer recipes. Deterministic, and quick enough for startup.
[[nodiscard]] std::vector<audio::Sound> make_sound_bank();

struct CueEvent {
    Cue cue = Cue::step;
    float gain = 1;
    float pitch = 1;
    std::optional<Vec3> position = std::nullopt; // Heard from the world, or else from the player.
    float reach = 30;
};

// Listens to the fixed ticks and decides what to play. Presentation only, like the
// character: it reads the simulation and never changes it.
class SoundDirector {
  public:
    // Starts listening to a (new) simulation without sounding what already happened.
    void reset(const Simulation &simulation);
    // After each tick, with the player's state from before it. Appends one-shot sounds.
    void tick(const PlayerState &before, const Simulation &simulation, float dt,
              std::vector<CueEvent> &sounds);
    // Looping sounds and their levels after the latest tick (gain 0 fades one out).
    [[nodiscard]] const std::vector<CueEvent> &loops() const {
        return m_loops;
    }

  private:
    float m_stride = 0; // Ground travelled since the last footstep.
    bool m_left = false;
    std::size_t m_found = 0;
    std::size_t m_checkpoint = 0;
    float m_wind = 0;
    std::vector<std::int64_t> m_ticks; // Per mover: tick sounds made so far.
    std::vector<Vec3> m_positions;     // Per mover: where it was last tick.
    std::vector<bool> m_moving;        // Per mover: whether it was moving.
    std::vector<CueEvent> m_loops;
};

} // namespace mj::game
