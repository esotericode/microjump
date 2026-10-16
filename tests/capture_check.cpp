#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <mj/core/png.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <string_view>
#include <vector>

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

Pixels load(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() <= 0 || stream.tellg() > 32 * 1024 * 1024)
        return {};
    const auto length = stream.tellg();
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), length);
    Pixels result;
    int channels = 0;
    auto *data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &result.width,
                                       &result.height, &channels, 4);
    if (data == nullptr)
        return {};
    result.rgba.assign(data, data + static_cast<std::size_t>(result.width) *
                                        static_cast<std::size_t>(result.height) * 4);
    stbi_image_free(data);
    return result;
}

bool scene_pixels(const Pixels &image, double aspect) {
    if (image.width < 640 || image.height < 360 || image.rgba.empty() ||
        std::abs(static_cast<double>(image.width) / image.height - aspect) > 0.02)
        return false;
    std::set<unsigned int> colors;
    for (int y = image.height / 3; y < image.height; y += 7) {
        for (int x = 0; x < image.width; x += 7) {
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) *
                4;
            const auto *p = image.rgba.data() + offset;
            if (p[3] != 255)
                return false;
            colors.insert((static_cast<unsigned int>(p[0] >> 2U) << 12U) |
                          (static_cast<unsigned int>(p[1] >> 2U) << 6U) |
                          static_cast<unsigned int>(p[2] >> 2U));
        }
    }
    return colors.size() > 64; // A clear-only image or overlay-only capture fails.
}

struct LightingStats {
    double mean_luminance = 0;
    double warm = 0;  // Fraction of warm, bright pixels (street-lamp pools).
    double vivid = 0; // Fraction of strongly colored pixels (colored local lights).
};
LightingStats lighting_stats(const Pixels &image) {
    LightingStats stats;
    const auto count = static_cast<double>(image.width) * image.height;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const int r = image.rgba[i];
        const int g = image.rgba[i + 1];
        const int b = image.rgba[i + 2];
        stats.mean_luminance += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        stats.warm += r > 120 && r - b > 40 ? 1 : 0;
        stats.vivid += std::max({r, g, b}) - std::min({r, g, b}) > 100 ? 1 : 0;
    }
    stats.mean_luminance /= count;
    stats.warm /= count;
    stats.vivid /= count;
    return stats;
}

// Day/night and local-light evidence: thresholds sit well inside the margins measured on
// Intel and software Vulkan (day 182 vs night 84 luminance; 10.7% warm lamp pixels at
// night vs 2.2% by day; 10.5% vivid pixels in the pavilion at night).
int check_lighting(const char *day_path, const char *night_path, const char *pavilion_path) {
    const auto day = load(day_path);
    const auto night = load(night_path);
    const auto pavilion = load(pavilion_path);
    if (!scene_pixels(day, 16.0 / 9) || !scene_pixels(night, 16.0 / 9) ||
        !scene_pixels(pavilion, 16.0 / 9)) {
        std::cerr << "Invalid or empty lighting capture\n";
        return 1;
    }
    const auto d = lighting_stats(day);
    const auto n = lighting_stats(night);
    const auto p = lighting_stats(pavilion);
    std::cout << "day luminance " << d.mean_luminance << ", night " << n.mean_luminance
              << "; warm day " << d.warm << ", night " << n.warm << "; pavilion vivid " << p.vivid
              << "\n";
    if (d.mean_luminance < n.mean_luminance + 40) {
        std::cerr << "Night is not clearly darker than day\n";
        return 1;
    }
    if (n.warm < 0.03 || n.warm < d.warm * 2) {
        std::cerr << "Street lamps do not visibly light the night scene\n";
        return 1;
    }
    if (p.vivid < 0.04) {
        std::cerr << "Colored pavilion lights are not visible\n";
        return 1;
    }
    std::cout << "Lighting captures: day/night contrast, lamp pools and colored lights verified\n";
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 5 && std::string_view(argv[1]) == "--lighting")
        return check_lighting(argv[2], argv[3], argv[4]);
    if (argc != 5) {
        std::cerr << "Expected baseline, camera, resized and replay captures\n";
        return 1;
    }
    const auto baseline = load(argv[1]);
    const auto camera = load(argv[2]);
    const auto resized = load(argv[3]);
    if (!scene_pixels(baseline, 16.0 / 9) || !scene_pixels(camera, 16.0 / 9) ||
        !scene_pixels(resized, 8.0 / 5) || baseline.width != camera.width ||
        baseline.height != camera.height ||
        (baseline.width == resized.width && baseline.height == resized.height) ||
        !scene_pixels(load(argv[4]), 16.0 / 9)) {
        std::cerr << "Invalid/empty scene capture or incorrect resize dimensions\n";
        return 1;
    }
    std::size_t changes = 0, samples = 0;
    for (int y = baseline.height / 2; y < baseline.height * 9 / 10; y += 3) {
        for (int x = baseline.width / 4; x < baseline.width * 3 / 4; x += 3) {
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(baseline.width) +
                 static_cast<std::size_t>(x)) *
                4;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                changes += static_cast<std::size_t>(
                    std::abs(static_cast<int>(baseline.rgba[offset + channel]) -
                             static_cast<int>(camera.rgba[offset + channel])));
                ++samples;
            }
        }
    }
    if (static_cast<double>(changes) / static_cast<double>(samples) < 3) {
        std::cerr << "Scripted camera did not change the 3D scene pixels\n";
        return 1;
    }
    // Validate PNG channel order and row order on a known 2x2 pattern, using the same writer.
    const std::array<std::uint8_t, 16> pattern{255, 0, 0,   255, 0,   255, 0,   255,
                                               0,   0, 255, 255, 255, 255, 255, 255};
    const auto roundtrip = std::filesystem::path(argv[1]).parent_path() / "png-roundtrip.png";
    if (!mj::write_png(roundtrip, 2, 2, pattern))
        return 1;
    const auto decoded = load(roundtrip);
    if (decoded.width != 2 || decoded.height != 2 ||
        decoded.rgba != std::vector<unsigned char>(pattern.begin(), pattern.end()))
        return 1;
    std::cout << "GPU PNGs: scene, replay, camera change, resize and RGBA orientation verified\n";
}
