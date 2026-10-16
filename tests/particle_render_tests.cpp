#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <mj/platform/window.hpp>
#include <mj/render/renderer.hpp>
#include <mj/render/vulkan_context.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    // Average a small patch, away from geometry edges and pixel-center differences.
    [[nodiscard]] std::array<double, 3> patch(int x, int y) const {
        std::array<double, 3> sum{};
        for (int row = y - 2; row <= y + 2; ++row) {
            for (int column = x - 2; column <= x + 2; ++column) {
                const auto offset =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(column)) *
                    4;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    sum[channel] += rgba[offset + channel] / 25.0;
            }
        }
        return sum;
    }
};

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

Pixels load(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(stream && stream.tellg() > 0 && stream.tellg() < 32 * 1024 * 1024,
            "Missing or invalid capture: " + path.string());
    const auto length = stream.tellg();
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), length);
    Pixels image;
    int channels = 0;
    auto *data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &image.width,
                                       &image.height, &channels, 4);
    require(data != nullptr, "PNG decode failed: " + path.string());
    image.rgba.assign(data, data + static_cast<std::size_t>(image.width) *
                                       static_cast<std::size_t>(image.height) * 4);
    stbi_image_free(data);
    require(image.width >= 320 && image.height >= 240, "Unexpected capture dimensions");
    for (std::size_t offset = 3; offset < image.rgba.size(); offset += 4)
        require(image.rgba[offset] == 255, "Particle blending changed output alpha");
    return image;
}

double difference(const std::array<double, 3> &a, const std::array<double, 3> &b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}

void run(const std::filesystem::path &directory, mj::ValidationLog &validation) {
    std::filesystem::create_directories(directory);
    auto window = mj::Window::create();
    require(window.has_value(), window ? "" : window.error().message);
    const auto resized = (*window)->resize(640, 480);
    require(resized.has_value(), resized ? "" : resized.error().message);
    auto context = mj::VulkanContext::create(**window, validation);
    require(context.has_value(), context ? "" : context.error().message);

    // A small central screen, with ample particle visible to either side. Its exact
    // illumination does not matter: occlusion is compared with its own baseline.
    mj::SceneMesh screen;
    screen.add_box({{0, 0, -3}, {0.6F, 0.8F, 0.1F}, {0.2F, 0.2F, 0.2F}});
    const std::array meshes{screen};
    auto renderer = mj::Renderer::create(**window, **context, meshes);
    require(renderer.has_value(), renderer ? "" : renderer.error().message);
    mj::FrameView view;
    view.camera = {{0, 0, 0}, 0, 0};
    view.environment.sun_color = {};
    view.environment.sky_zenith = {0.025F, 0.05F, 0.11F};
    view.environment.sky_horizon = view.environment.sky_zenith;
    view.environment.ground = view.environment.sky_zenith;
    view.environment.fog_density = 0;
    view.shadows.quality = mj::ShadowQuality::low;
    view.overlay = false;

    const auto capture = [&](const char *name, std::span<const mj::ParticleInstance> particles,
                             std::span<const mj::DrawInstance> instances = {}) {
        const auto path = directory / name;
        view.particles = particles;
        view.instances = instances;
        view.capture = &path;
        bool captured = false;
        for (int attempt = 0; attempt < 8 && !captured; ++attempt) {
            (void)(*window)->poll_events(
                [&](const SDL_Event &event) { (*renderer)->process_event(event); }, {});
            const auto drawn = (*renderer)->draw(view);
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            captured = *drawn;
        }
        require(captured, "Window never became drawable");
        view.capture = nullptr;
        return load(path);
    };

    const auto background = capture("particles-background.png", {});
    const int x = background.width / 2, y = background.height / 2;
    // The renderer uses a 60 degree vertical field of view. This also scales with
    // display DPI, without relying on the window manager honoring a physical size.
    const float projected_radius =
        static_cast<float>(background.height) / (2 * std::tan(mj::k_pi / 6)) / 5;
    const auto sample_x = [&](float radius) {
        return x + static_cast<int>(std::lround(projected_radius * radius));
    };
    const mj::ParticleInstance red{.position = {0, 0, -5},
                                   .radius = 1,
                                   .color = {0.8F, 0.01F, 0.01F},
                                   .opacity = 0.45F,
                                   .emissive = 1};
    auto opaque_red = red;
    opaque_red.opacity = 1;
    const auto half = capture("particles-half.png", std::array{red});
    const auto full = capture("particles-full.png", std::array{opaque_red});
    const auto base_center = background.patch(x, y);
    const auto half_center = half.patch(x, y);
    const auto full_center = full.patch(x, y);
    require(half_center[0] > base_center[0] + 20 && full_center[0] > half_center[0] + 20,
            "Opacity does not blend particle color with the background");
    require(half_center[2] > full_center[2] + 10,
            "Translucent particles replaced the background instead of retaining it");
    const double center_change = difference(full_center, base_center);
    const double edge_change =
        difference(full.patch(sample_x(0.85F), y), background.patch(sample_x(0.85F), y));
    require(edge_change > 2 && edge_change < center_change * 0.7,
            "The radial particle edge is not visibly softer than its center");
    require(difference(full.patch(sample_x(1.15F), y), background.patch(sample_x(1.15F), y)) <= 1,
            "The billboard leaked outside its disc");

    const std::array instances{mj::DrawInstance{.mesh = {0}, .model = mj::yaw_translation(0, {})}};
    const auto screen_only = capture("particles-screen.png", {}, instances);
    const auto occluded = capture("particles-occluded.png", std::array{opaque_red}, instances);
    require(difference(screen_only.patch(x, y), occluded.patch(x, y)) <= 1,
            "Opaque geometry did not occlude the particle behind it");
    require(difference(screen_only.patch(sample_x(0.75F), y), occluded.patch(sample_x(0.75F), y)) >
                10,
            "Occlusion fixture did not leave a visible particle beside the screen");

    auto near_green = red;
    near_green.position.z = -4;
    near_green.radius = 0.8F;
    near_green.color = {0.01F, 0.8F, 0.01F};
    auto far_red = red;
    far_red.position.z = -6;
    far_red.radius = 1.2F;
    const auto layers = capture("particles-layers.png", std::array{far_red, near_green});
    const auto reversed = capture("particles-reversed.png", std::array{near_green, far_red});
    require(layers.width == reversed.width && layers.height == reversed.height,
            "Capture size changed between order checks");
    for (std::size_t offset = 0; offset < layers.rgba.size(); ++offset)
        require(std::abs(static_cast<int>(layers.rgba[offset]) -
                         static_cast<int>(reversed.rgba[offset])) <= 1,
                "Different-depth particles depend on submission order");

    // Different-depth sorted layers alone cannot reveal accidental depth writes:
    // the nearer disc passes either way. Coincident discs with GREATER depth must
    // both contribute; writing the first disc's depth would reject the second one.
    auto green = red;
    green.color = near_green.color;
    const auto green_only = capture("particles-green.png", std::array{green});
    const auto coincident = capture("particles-coincident.png", std::array{red, green});
    const auto mixed = coincident.patch(x, y);
    require(mixed[0] > green_only.patch(x, y)[0] + 20 && mixed[1] > half_center[1] + 20,
            "Overlapping translucent discs blocked one another with depth writes");
    std::cout << "Particle GPU captures verify radial edges, alpha blending, opaque occlusion, "
                 "depth sorting, coincident layers and opaque PNG alpha\n";
}
} // namespace

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Expected a capture output directory\n";
        return 1;
    }
    mj::ValidationLog validation;
    bool passed = false;
    try {
        run(argv[1], validation);
        passed = true;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    // The window, renderer and context have all been destroyed, so destruction
    // errors are included as well as errors from recording and submission.
    if (validation.error_count != 0) {
        std::cerr << validation.error_count << " Vulkan validation errors\n";
        passed = false;
    }
    return passed ? 0 : 1;
}
