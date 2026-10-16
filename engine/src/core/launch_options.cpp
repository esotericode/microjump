#include <mj/core/launch_options.hpp>

#include <charconv>

namespace mj {

std::expected<LaunchOptions, Error>
parse_launch_options(std::span<const std::string_view> arguments) {
    LaunchOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--smoke-test") {
            options.smoke_test = true;
        } else if (argument == "--resize-test") {
            options.resize_test = true;
        } else if (argument == "--camera-test") {
            options.camera_test = true;
        } else if (argument == "--hide-overlay") {
            options.hide_overlay = true;
        } else if (argument == "--pacing-test") {
            options.pacing_test = true;
        } else if (argument == "--travel-test") {
            options.travel_test = true;
        } else if (argument == "--time-of-day") {
            ++index;
            float hours = -1;
            const auto value = index < arguments.size() ? arguments[index] : std::string_view{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), hours);
            if (value.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size() || !(hours >= 0 && hours <= 24) ||
                options.time_of_day)
                return std::unexpected(Error{"--time-of-day needs one value from 0 to 24 (hours)"});
            options.time_of_day = hours;
        } else if (argument == "--shadow-quality") {
            ++index;
            const auto value = index < arguments.size() ? arguments[index] : std::string_view{};
            std::optional<ShadowQuality> quality;
            for (const auto candidate :
                 {ShadowQuality::low, ShadowQuality::medium, ShadowQuality::high}) {
                std::string name(to_string(candidate));
                name[0] = static_cast<char>(name[0] - 'A' + 'a');
                if (value == name)
                    quality = candidate;
            }
            if (!quality || options.shadow_quality)
                return std::unexpected(Error{"--shadow-quality needs one of low, medium, high"});
            options.shadow_quality = quality;
        } else if (argument == "--frames" || argument == "--capture-frame" ||
                   argument == "--capture-tick") {
            ++index;
            if (index == arguments.size())
                return std::unexpected(Error{"Missing frame count"});
            const auto value = arguments[index];
            std::uint32_t count = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                count == 0 || count > 1'000'000) {
                return std::unexpected(Error{"Frame count must be an integer from 1 to 1000000"});
            }
            auto &destination = argument == "--frames"          ? options.frames
                                : argument == "--capture-frame" ? options.capture_frame
                                                                : options.capture_tick;
            if (destination)
                return std::unexpected(Error{"Duplicate frame count option"});
            destination = count;
        } else if (argument == "--capture-out" || argument == "--record" ||
                   argument == "--replay" || argument == "--level" || argument == "--frame-log") {
            auto &destination = argument == "--capture-out" ? options.capture_out
                                : argument == "--record"    ? options.record
                                : argument == "--replay"    ? options.replay
                                : argument == "--level"     ? options.level
                                                            : options.frame_log;
            ++index;
            if (index == arguments.size() || arguments[index].empty() || !destination.empty()) {
                return std::unexpected(Error{std::string(argument) + " needs one nonempty path"});
            }
            std::u8string encoded;
            for (const char c : arguments[index]) {
                encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
            }
            destination = std::filesystem::path(encoded);
        } else {
            return std::unexpected(Error{"Unknown argument: " + std::string(argument)});
        }
    }
    if (options.capture_frame && options.capture_tick)
        return std::unexpected(Error{"Use either --capture-frame or --capture-tick"});
    if ((options.capture_frame || options.capture_tick) != !options.capture_out.empty()) {
        return std::unexpected(Error{
            "Use --capture-frame N (or --capture-tick N) and --capture-out PATH.png together"});
    }
    if (!options.record.empty() && !options.replay.empty())
        return std::unexpected(Error{"--record and --replay cannot be combined"});
    if ((options.camera_test || options.resize_test || options.travel_test) &&
        (!options.record.empty() || !options.replay.empty()))
        return std::unexpected(Error{"Scripted render tests do not use recorded input"});
    if (options.frames && options.capture_frame && *options.frames < *options.capture_frame) {
        return std::unexpected(Error{"--frames must not end the run before the capture frame"});
    }
    if (!options.frames && !options.capture_frame && !options.capture_tick) {
        if (options.resize_test || options.camera_test)
            options.frames = 36;
        else if (options.travel_test)
            options.frames = 16;
        else if (options.smoke_test)
            options.frames = 12;
    }
    return options;
}

} // namespace mj
