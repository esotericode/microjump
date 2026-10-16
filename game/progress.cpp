#include "progress.hpp"

#include "level_io.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <initializer_list>
#include <set>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace mj::game {
namespace {
using Json = nlohmann::json;
constexpr std::size_t k_max_progress_bytes = 1'048'576;
constexpr std::size_t k_max_saved_levels = 64;

std::string display_path(const std::filesystem::path &path) {
    std::string text;
    try {
        for (const char8_t byte : path.u8string())
            text.push_back(static_cast<char>(byte));
    } catch (const std::filesystem::filesystem_error &) {
        return "<unrepresentable path>";
    }
    return text;
}

bool bounded_depth(std::string_view text) {
    int depth = 0;
    bool quoted = false, escaped = false;
    for (const char c : text) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > 16)
                return false;
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

class Reader {
  public:
    std::string error;

    void fail(const std::string &path, std::string_view reason) {
        if (error.empty())
            error = path + ": " + std::string(reason);
    }
    bool object(const Json &value, const std::string &path,
                std::initializer_list<std::string_view> keys) {
        if (!value.is_object()) {
            fail(path, "expected an object");
            return false;
        }
        for (auto item = value.begin(); item != value.end(); ++item) {
            if (std::find(keys.begin(), keys.end(), item.key()) == keys.end())
                fail(path + "." + item.key(), "unknown field");
        }
        for (const auto key : keys) {
            if (!value.contains(key))
                fail(path + "." + std::string(key), "missing field");
        }
        return error.empty();
    }
    std::string name(const Json &value, const std::string &path) {
        if (value.is_string()) {
            const auto &text = value.get_ref<const std::string &>();
            const bool printable = std::none_of(text.begin(), text.end(), [](char c) {
                const auto byte = static_cast<unsigned char>(c);
                return byte < 0x20 || byte == 0x7F;
            });
            if (!text.empty() && text.size() <= 80 && printable)
                return text;
        }
        fail(path, "expected a name of 1 to 80 UTF-8 bytes without control characters");
        return {};
    }
    float coordinate(const Json &value, const std::string &path) {
        if (value.is_number()) {
            const auto result = value.get<double>();
            if (std::isfinite(result) && result >= -4096 && result <= 4096)
                return static_cast<float>(result);
        }
        fail(path, "expected a finite coordinate in [-4096, 4096]");
        return 0;
    }
};

std::expected<std::string, Error> encode_progress(const Progress &progress) {
    Json root = {{"version", 1}, {"last_level", nullptr}, {"levels", Json::array()}};
    if (!progress.last_level.empty())
        root["last_level"] = progress.last_level;
    for (const auto &level : progress.levels) {
        root["levels"].push_back(
            {{"name", level.name},
             {"checkpoint", {level.checkpoint.x, level.checkpoint.y, level.checkpoint.z}},
             {"sparks", level.sparks}});
    }
    try {
        auto text = root.dump(2) + '\n';
        if (const auto valid = parse_progress(text); !valid)
            return std::unexpected(valid.error());
        return text;
    } catch (const Json::exception &) {
        return std::unexpected(Error{"Progress contains invalid UTF-8 text"});
    }
}

// Exclusive directory creation gives concurrent processes distinct staging files using only
// portable filesystem operations. The final rename stays on the destination's filesystem.
std::expected<std::filesystem::path, Error> staging_directory(const std::filesystem::path &parent) {
    static std::atomic<std::uint64_t> sequence{0};
    const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto candidate =
            parent / (".microjump-save-" + std::to_string(time) + "-" +
                      std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error))
            return candidate;
        if (error)
            return std::unexpected(Error{"Cannot stage progress: " + error.message()});
    }
    return std::unexpected(Error{"Cannot allocate a unique progress staging directory"});
}

class StagedSave {
  public:
    explicit StagedSave(std::filesystem::path directory)
        : m_directory(std::move(directory)), file(m_directory / "progress.tmp") {}
    ~StagedSave() {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(m_directory, ignored);
    }
    StagedSave(const StagedSave &) = delete;
    StagedSave &operator=(const StagedSave &) = delete;

  private:
    std::filesystem::path m_directory;

  public:
    std::filesystem::path file;
};

} // namespace

std::expected<Progress, Error> parse_progress(std::string_view text) {
    if (text.size() > k_max_progress_bytes || !bounded_depth(text))
        return std::unexpected(Error{"Progress exceeds the 1 MiB / 16-level nesting limit"});
    bool duplicate_key = false;
    std::vector<std::set<std::string>> keys;
    const auto check_keys = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.empty())
            duplicate_key = !keys.back().insert(value.get<std::string>()).second || duplicate_key;
        else if (event == Json::parse_event_t::object_end && !keys.empty())
            keys.pop_back();
        return true;
    };
    const auto root = Json::parse(text, check_keys, false);
    if (root.is_discarded())
        return std::unexpected(Error{"Progress is not valid JSON"});
    if (duplicate_key)
        return std::unexpected(Error{"Progress contains a duplicate object field"});
    Reader read;
    if (!read.object(root, "progress", {"version", "last_level", "levels"}))
        return std::unexpected(Error{read.error});
    if (!root["version"].is_number_integer() || root["version"] != 1)
        read.fail("progress.version", "expected format version 1");
    Progress progress;
    if (!root["last_level"].is_null()) {
        if (!root["last_level"].is_string() ||
            !valid_level_file(root["last_level"].get_ref<const std::string &>()))
            read.fail("progress.last_level", "expected a plain JSON level file name or null");
        else
            progress.last_level = root["last_level"].get<std::string>();
    }
    const auto &levels = root["levels"];
    if (!levels.is_array() || levels.size() > k_max_saved_levels)
        read.fail("progress.levels", "expected an array of at most 64 levels");
    std::set<std::string> level_names;
    if (read.error.empty()) {
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const auto path = "progress.levels[" + std::to_string(i) + "]";
            const auto &item = levels[i];
            if (!read.object(item, path, {"name", "checkpoint", "sparks"}))
                break;
            LevelProgress level;
            level.name = read.name(item["name"], path + ".name");
            if (!level_names.insert(level.name).second)
                read.fail(path + ".name", "level names must be unique");
            const auto &point = item["checkpoint"];
            if (!point.is_array() || point.size() != 3)
                read.fail(path + ".checkpoint", "expected three checkpoint coordinates");
            else
                level.checkpoint = {read.coordinate(point[0], path + ".checkpoint[0]"),
                                    read.coordinate(point[1], path + ".checkpoint[1]"),
                                    read.coordinate(point[2], path + ".checkpoint[2]")};
            const auto &sparks = item["sparks"];
            if (!sparks.is_array() || sparks.size() > 128)
                read.fail(path + ".sparks", "expected an array of at most 128 sparks");
            else {
                std::set<std::string> spark_names;
                for (std::size_t j = 0; j < sparks.size(); ++j) {
                    const auto spark_path = path + ".sparks[" + std::to_string(j) + "]";
                    auto name = read.name(sparks[j], spark_path);
                    if (!spark_names.insert(name).second)
                        read.fail(spark_path, "spark names must be unique");
                    level.sparks.push_back(std::move(name));
                }
            }
            progress.levels.push_back(std::move(level));
        }
    }
    if (!read.error.empty())
        return std::unexpected(Error{read.error});
    return progress;
}

std::expected<Progress, Error> load_progress(const std::filesystem::path &path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return std::unexpected(Error{"Cannot inspect progress: " + error.message()});
    if (!exists)
        return Progress{};
    const auto name = display_path(path);
    if (!std::filesystem::is_regular_file(path, error) || error)
        return std::unexpected(Error{"Progress is not a readable file: " + name});
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected(Error{"Cannot open progress: " + name});
    const auto size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(k_max_progress_bytes))
        return std::unexpected(Error{"Progress is empty or exceeds 1 MiB: " + name});
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(text.data(), size);
    if (!file)
        return std::unexpected(Error{"Cannot read progress: " + name});
    auto progress = parse_progress(text);
    if (!progress)
        progress.error().message = name + ": " + progress.error().message;
    return progress;
}

std::expected<void, Error> save_progress(const std::filesystem::path &path,
                                         const Progress &progress) {
    const auto text = encode_progress(progress);
    if (!text)
        return std::unexpected(text.error());
    if (path.empty() || path.filename().empty() || path.filename() == "." ||
        path.filename() == "..")
        return std::unexpected(Error{"Progress requires a file path"});
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error)
        return std::unexpected(Error{"Cannot create progress directory: " + error.message()});
    auto directory = staging_directory(parent);
    if (!directory)
        return std::unexpected(directory.error());
    const StagedSave staged(std::move(*directory));
    std::ofstream file(staged.file, std::ios::binary | std::ios::trunc);
    file.write(text->data(), static_cast<std::streamsize>(text->size()));
    file.close();
    if (!file)
        return std::unexpected(Error{"Cannot write staged progress: " + display_path(path)});
#ifdef _WIN32
    if (MoveFileExW(staged.file.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(staged.file, path, error);
#endif
    if (error)
        return std::unexpected(Error{"Cannot replace progress: " + error.message()});
    return {};
}

RestoredProgress restore_progress(const Progress &progress, const Level &level) {
    RestoredProgress restored;
    restored.collected.resize(level.collectibles.size(), false);
    const auto saved =
        std::find_if(progress.levels.begin(), progress.levels.end(),
                     [&](const LevelProgress &item) { return item.name == level.name; });
    if (saved == progress.levels.end())
        return restored;
    for (std::size_t i = 0; i < level.checkpoints.size(); ++i) {
        const auto point = level.checkpoints[i].position;
        if (point.x == saved->checkpoint.x && point.y == saved->checkpoint.y &&
            point.z == saved->checkpoint.z) {
            restored.checkpoint = i;
            break;
        }
    }
    for (std::size_t i = 0; i < level.collectibles.size(); ++i)
        restored.collected[i] = std::find(saved->sparks.begin(), saved->sparks.end(),
                                          level.collectibles[i].name) != saved->sparks.end();
    return restored;
}

std::expected<void, Error> remember_progress(Progress &progress, std::string_view level_file,
                                             const Level &level, std::size_t checkpoint,
                                             const std::vector<bool> &collected) {
    if ((!level_file.empty() && !valid_level_file(level_file)) ||
        checkpoint >= level.checkpoints.size() || collected.size() != level.collectibles.size())
        return std::unexpected(Error{"Cannot remember progress: invalid level snapshot"});
    Progress next = progress;
    next.last_level = level_file;
    LevelProgress saved{level.name, level.checkpoints[checkpoint].position, {}};
    for (std::size_t i = 0; i < collected.size(); ++i) {
        if (collected[i])
            saved.sparks.push_back(level.collectibles[i].name);
    }
    const auto found =
        std::find_if(next.levels.begin(), next.levels.end(),
                     [&](const LevelProgress &item) { return item.name == level.name; });
    if (found == next.levels.end())
        next.levels.push_back(std::move(saved));
    else
        *found = std::move(saved);
    if (const auto valid = encode_progress(next); !valid)
        return std::unexpected(valid.error());
    progress = std::move(next);
    return {};
}

} // namespace mj::game
