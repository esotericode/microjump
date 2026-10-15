#include <mj/core/png.hpp>

#include <stb_image_write.h>

#include <fstream>
#include <limits>

namespace mj {
std::expected<void, Error> write_png(const std::filesystem::path &path, std::uint32_t width,
                                     std::uint32_t height, std::span<const std::uint8_t> rgba) {
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        return std::unexpected(Error{"Invalid RGBA capture dimensions or byte count"});
    }
    std::error_code error;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return std::unexpected(Error{"Capture directory: " + error.message()});
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        return std::unexpected(Error{"Cannot open capture output"});
    // The stb callback ABI requires adjacent context/data void pointers.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const auto writer = [](void *context, void *data, int size) {
        static_cast<std::ofstream *>(context)->write(static_cast<const char *>(data), size);
    };
    const int result =
        stbi_write_png_to_func(writer, &stream, static_cast<int>(width), static_cast<int>(height),
                               4, rgba.data(), static_cast<int>(width * 4));
    stream.flush();
    if (result == 0 || !stream)
        return std::unexpected(Error{"Failed to write PNG capture"});
    return {};
}
} // namespace mj
