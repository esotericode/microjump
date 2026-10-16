#pragma once

#include "level.hpp"

#include <mj/core/error.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mj::game {

// Saved identities are authored names and positions, never array indices or player coordinates.
struct LevelProgress {
    std::string name;
    Vec3 checkpoint;
    std::vector<std::string> sparks;
};

struct Progress {
    std::string
        last_level; // Plain file name beside the starting level; empty for a new expedition.
    std::vector<LevelProgress> levels;
};

struct RestoredProgress {
    std::size_t checkpoint = 0;
    std::vector<bool> collected;
};

// Version 1 JSON is strictly bounded and rejects unknown/duplicate fields and identities.
[[nodiscard]] std::expected<Progress, Error> parse_progress(std::string_view text);
// A missing save starts an expedition. Every other read/format failure is an error: callers
// should preserve the existing file until the player explicitly starts a new expedition.
[[nodiscard]] std::expected<Progress, Error> load_progress(const std::filesystem::path &path);
// Stages beside the destination and atomically replaces it; a failed save keeps the old file.
[[nodiscard]] std::expected<void, Error> save_progress(const std::filesystem::path &path,
                                                       const Progress &progress);
// Reordered content keeps its identities; unknown sparks disappear and removed checkpoints
// fall back to the current spawn. Respawn yaw always comes from the current authored level.
[[nodiscard]] RestoredProgress restore_progress(const Progress &progress, const Level &level);
// Capture a loaded level without any dependency on the simulation. An empty file name stores a
// custom level's hunt without automatic filename resume. Failure leaves progress intact.
[[nodiscard]] std::expected<void, Error>
remember_progress(Progress &progress, std::string_view level_file, const Level &level,
                  std::size_t checkpoint, const std::vector<bool> &collected);

} // namespace mj::game
