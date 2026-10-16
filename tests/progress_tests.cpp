#include "test_framework.hpp"

#include <level_io.hpp>
#include <progress.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace {
constexpr std::string_view k_progress = R"({
  "version":1,"last_level":"lantern-isles.json",
  "levels":[{"name":"lantern-isles","checkpoint":[1,2,3],
             "sparks":["First light","Secret blossom"]}]
})";

std::string changed(std::string_view from, std::string_view to) {
    std::string text(k_progress);
    const auto offset = text.find(from);
    REQUIRE(offset != std::string::npos);
    text.replace(offset, from.size(), to);
    return text;
}

mj::game::Level test_level() {
    mj::game::Level level;
    level.name = "lantern-isles";
    level.checkpoints = {{{0, 0.01F, 0}, 0}, {{1, 2, 3}, 0.75F}};
    level.collectibles = {{"First light", {0, 1, 0}, 0.65F},
                          {"Unfound light", {3, 1, 0}, 0.65F},
                          {"Secret blossom", {5, 1, 0}, 0.65F}};
    return level;
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        static std::atomic<unsigned int> sequence{0};
        const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 32; ++i) {
            path = std::filesystem::current_path() /
                   ("progress-tests-" + std::to_string(time) + "-" +
                    std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            if (std::filesystem::create_directory(path))
                return;
        }
        REQUIRE(false);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    std::filesystem::path path;
};

std::string read_text(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    REQUIRE(file.good());
}

std::size_t entry_count(const std::filesystem::path &path) {
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(path),
                                                  std::filesystem::directory_iterator()));
}
} // namespace

TEST_CASE("saved expeditions use named sparks and current authored checkpoints") {
    const auto progress = mj::game::parse_progress(k_progress);
    REQUIRE(progress);
    CHECK(progress->last_level == "lantern-isles.json");
    REQUIRE(progress->levels.size() == 1);
    CHECK(progress->levels[0].checkpoint.y == 2.0F);
    auto level = test_level();
    const auto restored = mj::game::restore_progress(*progress, level);
    CHECK(restored.checkpoint == 1);
    CHECK(restored.collected == std::vector<bool>{true, false, true});

    std::swap(level.checkpoints[0], level.checkpoints[1]);
    std::swap(level.collectibles[0], level.collectibles[1]);
    const auto reordered = mj::game::restore_progress(*progress, level);
    CHECK(reordered.checkpoint == 0);
    CHECK(reordered.collected == std::vector<bool>{false, true, true});

    level.checkpoints[0].position.x += 0.001F;
    level.collectibles[2].name = "New blossom";
    const auto edited = mj::game::restore_progress(*progress, level);
    CHECK(edited.checkpoint == 0); // An obsolete point never becomes arbitrary player coordinates.
    CHECK(edited.collected == std::vector<bool>{false, true, false});
    level.name = "a-different-place";
    const auto different = mj::game::restore_progress(*progress, level);
    CHECK(different.checkpoint == 0);
    CHECK(different.collected == std::vector<bool>{false, false, false});

    level = test_level();
    level.checkpoints[1].position.x = std::nextafter(1.0F, 2.0F);
    CHECK(mj::game::restore_progress(*progress, level).checkpoint == 0);
}

TEST_CASE("progress parsing strictly rejects damaged, ambiguous and unsupported profiles") {
    CHECK(mj::game::parse_progress(R"({"version":1,"last_level":null,"levels":[]})"));
    CHECK_FALSE(mj::game::parse_progress(""));
    CHECK_FALSE(mj::game::parse_progress("{"));
    CHECK_FALSE(mj::game::parse_progress("[]"));
    for (const auto &[from, to] :
         {std::pair{"\"version\":1", "\"version\":2"},
          {"\"version\":1", "\"version\":1.0"},
          {"\"version\":1", "\"version\":true"},
          {"\"version\":1", "\"version\":1,\"version\":1"},
          {"\"version\":1,", ""},
          {"\"version\":1", "\"version\":1,\"typo\":0"},
          {"\"last_level\":\"lantern-isles.json\"", "\"last_level\":7"},
          {"\"lantern-isles.json\"", "\"../lantern-isles.json\""},
          {"\"lantern-isles.json\"", "\"sub/lantern-isles.json\""},
          {"\"lantern-isles.json\"", "\"C:\\\\lantern-isles.json\""},
          {"\"lantern-isles.json\"", "\"/lantern-isles.json\""},
          {"\"lantern-isles.json\"", "\".hidden.json\""},
          {"\"lantern-isles.json\"", "\"lantern-isles.txt\""},
          {"\"levels\":[", "\"levels\":null,\"other\":["},
          {"\"name\":\"lantern-isles\"", "\"name\":\"\""},
          {"\"name\":\"lantern-isles\"", "\"name\":null"},
          {"\"name\":\"lantern-isles\"", "\"name\":\"Bad\\nname\""},
          {"\"checkpoint\":[1,2,3]", "\"checkpoint\":[1,2]"},
          {"\"checkpoint\":[1,2,3]", "\"checkpoint\":[1,2,3,4]"},
          {"\"checkpoint\":[1,2,3]", "\"checkpoint\":[1,2,null]"},
          {"\"checkpoint\":[1,2,3]", "\"checkpoint\":[1,2,4096.1]"},
          {"\"checkpoint\":[1,2,3]", "\"checkpoint\":[-4096.1,2,3]"},
          {"\"checkpoint\":[1,2,3]", "\"checkpoint\":[1e999,2,3]"},
          {"\"checkpoint\":[1,2,3]", "\"position\":[1,2,3]"},
          {"\"sparks\":[\"First light\",\"Secret blossom\"]", "\"sparks\":null"},
          {"\"Secret blossom\"", "\"First light\""},
          {"\"Secret blossom\"", "\"\""},
          {"\"Secret blossom\"", "false"},
          {"\"Secret blossom\"", "\"Bad\\u007fname\""}}) {
        CAPTURE(from);
        CAPTURE(to);
        CHECK_FALSE(mj::game::parse_progress(changed(from, to)));
    }
    CHECK_FALSE(mj::game::parse_progress(std::string(17, '[') + std::string(17, ']')));
    CHECK_FALSE(mj::game::parse_progress(std::string(1024 * 1024 + 1, ' ')));
    CHECK_FALSE(mj::game::parse_progress(changed("First light", std::string(81, 'x'))));
    CHECK(mj::game::parse_progress(changed("First light", std::string(80, 'x'))));
    CHECK(mj::game::parse_progress(changed("[1,2,3]", "[-4096,4096,0]")));
    CHECK(mj::game::parse_progress(changed("Secret blossom", "{\\\"nested\\\"[text]}")));
}

TEST_CASE("named snapshots preserve other places and reject invalid changes without mutation") {
    mj::game::Progress progress;
    auto level = test_level();
    REQUIRE(
        mj::game::remember_progress(progress, "lantern-isles.json", level, 1, {true, false, true}));
    CHECK(progress.last_level == "lantern-isles.json");
    CHECK(progress.levels[0].sparks == std::vector<std::string>{"First light", "Secret blossom"});
    level.name = "microjump-playground";
    REQUIRE(
        mj::game::remember_progress(progress, "playground.json", level, 0, {false, true, false}));
    REQUIRE(progress.levels.size() == 2);
    CHECK(progress.last_level == "playground.json");
    CHECK(progress.levels[0].sparks.size() == 2);
    CHECK(progress.levels[1].sparks == std::vector<std::string>{"Unfound light"});
    REQUIRE(
        mj::game::remember_progress(progress, "playground.json", level, 1, {true, true, false}));
    CHECK(progress.levels.size() == 2);
    CHECK(progress.levels[1].checkpoint.y == 2.0F);

    CHECK_FALSE(
        mj::game::remember_progress(progress, "../bad.json", level, 0, {false, false, false}));
    CHECK_FALSE(
        mj::game::remember_progress(progress, "bad.json", level, 10, {false, false, false}));
    CHECK_FALSE(mj::game::remember_progress(progress, "bad.json", level, 0, {false}));
    level.checkpoints[0].position.x = std::numeric_limits<float>::infinity();
    CHECK_FALSE(mj::game::remember_progress(progress, "bad.json", level, 0, {false, false, false}));
    CHECK(progress.last_level == "playground.json");
    CHECK(progress.levels.size() == 2);
    CHECK(progress.levels[1].sparks == std::vector<std::string>{"First light", "Unfound light"});
}

TEST_CASE("progress enforces level and spark budgets and unique level identities") {
    mj::game::Progress progress;
    auto level = test_level();
    level.collectibles.clear();
    for (std::size_t i = 0; i < 128; ++i)
        level.collectibles.push_back({"Spark " + std::to_string(i), {}, 0.65F});
    REQUIRE(mj::game::remember_progress(progress, "lantern-isles.json", level, 0,
                                        std::vector<bool>(128, true)));
    CHECK(progress.levels[0].sparks.size() == 128);
    level.collectibles.push_back({"Too many", {}, 0.65F});
    CHECK_FALSE(mj::game::remember_progress(progress, "lantern-isles.json", level, 0,
                                            std::vector<bool>(129, true)));
    level.collectibles.clear();
    for (std::size_t i = 1; i < 64; ++i) {
        level.name = "Place " + std::to_string(i);
        REQUIRE(mj::game::remember_progress(progress, "other.json", level, 0, {}));
    }
    REQUIRE(progress.levels.size() == 64);
    level.name = "Place 64";
    CHECK_FALSE(mj::game::remember_progress(progress, "overflow.json", level, 0, {}));
    CHECK(progress.levels.size() == 64);
    CHECK(progress.last_level == "other.json");
    const std::string item = R"({"name":"same","checkpoint":[0,0,0],"sparks":[]})";
    CHECK_FALSE(mj::game::parse_progress(R"({"version":1,"last_level":null,"levels":[)" + item +
                                         "," + item + "]}"));
}

TEST_CASE("custom levels can remember named progress without a resumable plain filename") {
    auto progress = mj::game::parse_progress(k_progress);
    REQUIRE(progress);
    const auto level = test_level();
    REQUIRE(mj::game::remember_progress(*progress, {}, level, 1, {false, true, true}));
    CHECK(progress->last_level.empty());
    REQUIRE(progress->levels.size() == 1);
    CHECK(progress->levels[0].sparks ==
          std::vector<std::string>{"Unfound light", "Secret blossom"});
    const auto restored = mj::game::restore_progress(*progress, level);
    CHECK(restored.checkpoint == 1);
    CHECK(restored.collected == std::vector<bool>{false, true, true});
    CHECK_FALSE(mj::game::remember_progress(*progress, "My custom world.json", level, 0,
                                            {false, false, false}));
    CHECK(progress->last_level.empty());
    CHECK(progress->levels[0].sparks.size() == 2);
}

TEST_CASE("progress saves replace atomically on Unicode paths and failed writes retain old data") {
    TemporaryDirectory directory;
    const auto path =
        directory.path / std::filesystem::path(u8"expedition-\u96ea") / "progress.json";
    const auto missing = mj::game::load_progress(path);
    REQUIRE(missing);
    CHECK(missing->last_level.empty());
    CHECK(missing->levels.empty());
    CHECK_FALSE(std::filesystem::exists(path.parent_path()));
    auto progress = mj::game::parse_progress(k_progress);
    REQUIRE(progress);
    REQUIRE(mj::game::save_progress(path, *progress));
    const auto loaded = mj::game::load_progress(path);
    REQUIRE(loaded);
    CHECK(loaded->last_level == "lantern-isles.json");
    CHECK(loaded->levels[0].sparks.size() == 2);
    CHECK(entry_count(path.parent_path()) == 1);
    progress->levels[0].sparks.clear();
    progress->levels[0].checkpoint = {1.2345678F, 0.01F, -6.123456F};
    REQUIRE(mj::game::save_progress(path, *progress));
    const auto replaced = mj::game::load_progress(path);
    REQUIRE(replaced);
    CHECK(replaced->levels[0].sparks.empty());
    CHECK(replaced->levels[0].checkpoint.x == progress->levels[0].checkpoint.x);
    CHECK(replaced->levels[0].checkpoint.y == progress->levels[0].checkpoint.y);
    CHECK(replaced->levels[0].checkpoint.z == progress->levels[0].checkpoint.z);
    const auto old_bytes = read_text(path);
    progress->last_level = "../escape.json";
    CHECK_FALSE(mj::game::save_progress(path, *progress));
    CHECK(read_text(path) == old_bytes);
    progress->last_level = "playground.json";
    progress->levels[0].sparks = {std::string(1, static_cast<char>(0xFF))};
    CHECK_FALSE(mj::game::save_progress(path, *progress));
    CHECK(read_text(path) == old_bytes);
    CHECK(entry_count(path.parent_path()) == 1);

    const auto blocker = path.parent_path() / "occupied";
    REQUIRE(std::filesystem::create_directory(blocker));
    write_text(blocker / "keep.txt", "Do not replace this directory");
    CHECK_FALSE(mj::game::save_progress(blocker, {}));
    CHECK(read_text(blocker / "keep.txt") == "Do not replace this directory");
    CHECK(entry_count(path.parent_path()) == 2);
    CHECK_FALSE(mj::game::save_progress(path / "not-a-directory.json", {}));
    CHECK(read_text(path) == old_bytes);
}

TEST_CASE("missing progress is new while corrupt, oversized and non-file saves remain errors") {
    TemporaryDirectory directory;
    const auto path = directory.path / "progress.json";
    CHECK(mj::game::load_progress(path));
    CHECK_FALSE(mj::game::load_progress(directory.path));
    for (const auto &text :
         {std::string{}, std::string("{broken"), changed("\"version\":1", "\"version\":999"),
          std::string(1024 * 1024 + 1, ' ')}) {
        write_text(path, text);
        CHECK_FALSE(mj::game::load_progress(path));
        CHECK(read_text(path) == text);
    }
    REQUIRE(mj::game::save_progress(path, {}));
    const auto reset = mj::game::load_progress(path);
    REQUIRE(reset);
    CHECK(reset->last_level.empty());
    CHECK(reset->levels.empty());
    CHECK(entry_count(directory.path) == 1);
}

TEST_CASE("travel and saved progress share the plain level filename contract") {
    for (const auto filename : {"playground.json", "lantern-isles.json", "yard_2.json"})
        CHECK(mj::game::valid_level_file(filename));
    for (const auto filename : {"", ".json", "../a.json", "a/b.json", "a\\b.json", ".hidden.json",
                                "C:a.json", "a.JSON", "a.json\n"})
        CHECK_FALSE(mj::game::valid_level_file(filename));
}
