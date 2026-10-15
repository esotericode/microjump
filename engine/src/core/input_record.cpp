#include <mj/core/input_record.hpp>

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mj {
namespace {
constexpr std::string_view k_magic = "microjump-replay 1";
constexpr std::string_view k_rate = "tick-rate 120";
constexpr std::uint64_t k_max_ticks = 120ULL * 60 * 60 * 4; // Four hours.
constexpr std::size_t k_max_bytes = 64ULL * 1024 * 1024;

bool same_bits(const TickInput &a, const TickInput &b) {
    return std::bit_cast<std::uint32_t>(a.move_x) == std::bit_cast<std::uint32_t>(b.move_x) &&
           std::bit_cast<std::uint32_t>(a.move_y) == std::bit_cast<std::uint32_t>(b.move_y) &&
           std::bit_cast<std::uint32_t>(a.camera_yaw) ==
               std::bit_cast<std::uint32_t>(b.camera_yaw) &&
           std::bit_cast<std::uint32_t>(a.camera_pitch) ==
               std::bit_cast<std::uint32_t>(b.camera_pitch) &&
           a.buttons == b.buttons;
}

void append(std::string &out, float value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), result.ptr);
}

template <typename T> bool parse_field(std::string_view field, T &value) {
    const auto result = std::from_chars(field.data(), field.data() + field.size(), value);
    return result.ec == std::errc{} && result.ptr == field.data() + field.size();
}

Error line_error(std::size_t line, std::string_view message) {
    return {"Replay line " + std::to_string(line) + ": " + std::string(message)};
}
} // namespace

std::string serialize_replay(std::span<const TickInput> ticks) {
    std::string out;
    out.append(k_magic).append("\n").append(k_rate).append("\n");
    out.append("# count move_x move_y camera_yaw camera_pitch buttons\n");
    for (std::size_t i = 0; i < ticks.size();) {
        std::size_t run = 1;
        while (i + run < ticks.size() && same_bits(ticks[i], ticks[i + run]))
            ++run;
        const auto &tick = ticks[i];
        out.append(std::to_string(run));
        for (const float value : {tick.move_x, tick.move_y, tick.camera_yaw, tick.camera_pitch}) {
            out.push_back(' ');
            append(out, value);
        }
        out.push_back(' ');
        out.append(std::to_string(tick.buttons)).push_back('\n');
        i += run;
    }
    return out;
}

std::expected<std::vector<TickInput>, Error> parse_replay(std::string_view text) {
    std::vector<TickInput> ticks;
    std::size_t line_number = 0;
    bool header = false;
    bool rate = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++line_number;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!header) {
            if (line != k_magic)
                return std::unexpected(Error{"Not a microjump replay (version 1)"});
            header = true;
            continue;
        }
        if (!rate) {
            if (line != k_rate)
                return std::unexpected(line_error(line_number, "expected 'tick-rate 120'"));
            rate = true;
            continue;
        }
        if (line.empty() || line.front() == '#')
            continue;
        std::array<std::string_view, 6> fields{};
        std::size_t count = 0;
        while (!line.empty()) {
            const auto start = line.find_first_not_of(' ');
            if (start == std::string_view::npos)
                break;
            line.remove_prefix(start);
            const auto stop = line.find(' ');
            if (count == fields.size())
                return std::unexpected(line_error(line_number, "too many fields"));
            fields[count++] = line.substr(0, stop);
            line = stop == std::string_view::npos ? std::string_view{} : line.substr(stop);
        }
        if (count != fields.size())
            return std::unexpected(line_error(line_number, "expected 6 fields"));
        std::uint64_t run = 0;
        TickInput tick;
        if (!parse_field(fields[0], run) || run == 0 || !parse_field(fields[1], tick.move_x) ||
            !parse_field(fields[2], tick.move_y) || !parse_field(fields[3], tick.camera_yaw) ||
            !parse_field(fields[4], tick.camera_pitch) || !parse_field(fields[5], tick.buttons))
            return std::unexpected(line_error(line_number, "malformed number"));
        for (const float value : {tick.move_x, tick.move_y, tick.camera_yaw, tick.camera_pitch}) {
            if (!std::isfinite(value))
                return std::unexpected(line_error(line_number, "non-finite value"));
        }
        if (tick.move_x * tick.move_x + tick.move_y * tick.move_y > 1.0001F)
            return std::unexpected(line_error(line_number, "move input longer than 1"));
        if ((tick.buttons & ~input_button::all) != 0)
            return std::unexpected(line_error(line_number, "unknown button bits"));
        if (run > k_max_ticks - ticks.size())
            return std::unexpected(line_error(line_number, "replay longer than four hours"));
        ticks.insert(ticks.end(), static_cast<std::size_t>(run), tick);
    }
    if (!rate)
        return std::unexpected(Error{"Replay header is incomplete"});
    return ticks;
}

std::expected<void, Error> save_replay(const std::filesystem::path &path,
                                       std::span<const TickInput> ticks) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const auto text = serialize_replay(ticks);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file)
        return std::unexpected(Error{"Cannot write replay: " + path.string()});
    return {};
}

std::expected<std::vector<TickInput>, Error> load_replay(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected(Error{"Cannot open replay: " + path.string()});
    const auto size = file.tellg();
    if (size < 0 || static_cast<std::size_t>(size) > k_max_bytes)
        return std::unexpected(Error{"Replay file is too large"});
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(text.data(), size);
    if (!file)
        return std::unexpected(Error{"Cannot read replay: " + path.string()});
    return parse_replay(text);
}

} // namespace mj
