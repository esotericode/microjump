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
        } else if (argument == "--frames" || argument == "--capture-frame") {
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
            auto &destination = argument == "--frames" ? options.frames : options.capture_frame;
            if (destination)
                return std::unexpected(Error{"Duplicate frame count option"});
            destination = count;
        } else if (argument == "--capture-out" || argument == "--record" ||
                   argument == "--replay") {
            auto &destination = argument == "--capture-out" ? options.capture_out
                                : argument == "--record"    ? options.record
                                                            : options.replay;
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
    if (options.capture_frame.has_value() != !options.capture_out.empty()) {
        return std::unexpected(Error{"Use --capture-frame N and --capture-out PATH.png together"});
    }
    if (!options.record.empty() && !options.replay.empty())
        return std::unexpected(Error{"--record and --replay cannot be combined"});
    if ((options.camera_test || options.resize_test) &&
        (!options.record.empty() || !options.replay.empty()))
        return std::unexpected(Error{"Scripted render tests do not use recorded input"});
    if (options.frames && options.capture_frame && *options.frames < *options.capture_frame) {
        return std::unexpected(Error{"--frames must not end the run before the capture frame"});
    }
    if (!options.frames && !options.capture_frame) {
        if (options.resize_test || options.camera_test)
            options.frames = 36;
        else if (options.smoke_test)
            options.frames = 12;
    }
    return options;
}

} // namespace mj
