#include <mj/core/launch_options.hpp>

namespace mj {

std::expected<LaunchOptions, Error>
parse_launch_options(std::span<const std::string_view> arguments) {
    LaunchOptions options;
    for (const auto argument : arguments) {
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--smoke-test") {
            options.smoke_test = true;
        } else {
            return std::unexpected(Error{"Unknown argument: " + std::string(argument)});
        }
    }
    return options;
}

} // namespace mj
