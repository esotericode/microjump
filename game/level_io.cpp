#include "level_io.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <initializer_list>
#include <set>

namespace mj::game {
namespace {
using Json = nlohmann::json;
constexpr std::size_t k_max_level_bytes = 2ULL * 1024 * 1024;

std::string display_path(const std::filesystem::path &path) {
    std::string text;
    for (const char8_t byte : path.u8string())
        text.push_back(static_cast<char>(byte));
    return text;
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
        return true;
    }
    const Json &member(const Json &object, std::string_view key, const std::string &path,
                       bool required = true) {
        static const Json missing;
        const auto found = object.find(key);
        if (found != object.end())
            return *found;
        if (required)
            fail(path + "." + std::string(key), "missing field");
        return missing;
    }
    float number(const Json &value, const std::string &path, float low, float high) {
        if (value.is_number()) {
            const double result = value.get<double>();
            if (std::isfinite(result) && result >= low && result <= high)
                return static_cast<float>(result);
        }
        fail(path, "expected a finite number in [" + std::to_string(low) + ", " +
                       std::to_string(high) + "]");
        return low;
    }
    float scalar(const Json &object, std::string_view key, const std::string &path, float low,
                 float high, float fallback) {
        const auto found = object.find(key);
        return found == object.end() ? fallback
                                     : number(*found, path + "." + std::string(key), low, high);
    }
    Vec3 vector(const Json &value, const std::string &path, float low, float high) {
        if (!value.is_array() || value.size() != 3) {
            fail(path, "expected an array of three numbers");
            return {};
        }
        return {number(value[0], path + "[0]", low, high),
                number(value[1], path + "[1]", low, high),
                number(value[2], path + "[2]", low, high)};
    }
    std::string name(const Json &value, const std::string &path) {
        if (value.is_string()) {
            const auto &text = value.get_ref<const std::string &>();
            // Control characters would break one-line uses such as replay headers.
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
    bool array(const Json &value, const std::string &path, std::size_t low, std::size_t high) {
        if (value.is_array() && value.size() >= low && value.size() <= high)
            return true;
        fail(path, "invalid array type or element count");
        return false;
    }
    Vec3 albedo(const Json &value, const std::string &path) {
        if (value.is_string()) {
            const auto &text = value.get_ref<const std::string &>();
            if (text.size() == 7 && text.front() == '#') {
                unsigned int rgb = 0;
                const auto result = std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
                if (result.ec == std::errc{} && result.ptr == text.data() + 7)
                    return linear_color(rgb);
            }
        }
        fail(path, "expected an sRGB color such as #42a5a0");
        return {};
    }
};

// Bound nesting before constructing the DOM. Brackets inside strings are data.
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
            if (++depth > 32)
                return false;
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}
} // namespace

std::expected<Level, Error> parse_level(std::string_view text) {
    if (text.size() > k_max_level_bytes || !bounded_depth(text))
        return std::unexpected(Error{"Level exceeds the 2 MiB / 32-level nesting limit"});
    bool duplicate_key = false;
    std::vector<std::set<std::string>> object_keys;
    const auto check_keys = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start)
            object_keys.emplace_back();
        else if (event == Json::parse_event_t::key && !object_keys.empty())
            duplicate_key =
                !object_keys.back().insert(value.get<std::string>()).second || duplicate_key;
        else if (event == Json::parse_event_t::object_end && !object_keys.empty())
            object_keys.pop_back();
        return true;
    };
    const auto root = Json::parse(text, check_keys, false);
    if (root.is_discarded())
        return std::unexpected(Error{"Level is not valid JSON"});
    if (duplicate_key)
        return std::unexpected(Error{"Level contains a duplicate object field"});
    Reader read;
    if (!read.object(
            root, "level",
            {"version", "name", "kill_height", "shapes", "checkpoints", "lights", "zones"}))
        return std::unexpected(Error{read.error});
    const auto &version = read.member(root, "version", "level");
    if (!version.is_number_integer() || version != 1)
        read.fail("level.version", "expected format version 1");
    Level level;
    level.name = read.name(read.member(root, "name", "level"), "level.name");
    level.kill_height = read.scalar(root, "kill_height", "level", -4096, 4096, -25);
    const auto &shapes = read.member(root, "shapes", "level");
    if (read.array(shapes, "level.shapes", 1, 2048)) {
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            const auto &item = shapes[i];
            const auto path = "level.shapes[" + std::to_string(i) + "]";
            if (!read.object(item, path,
                             {"kind", "center", "size", "color", "yaw", "material", "wall_jump"}))
                break;
            Shape shape;
            const auto &kind = read.member(item, "kind", path);
            if (kind == "box")
                shape.kind = ShapeKind::box;
            else if (kind == "ramp")
                shape.kind = ShapeKind::ramp;
            else if (kind == "cylinder")
                shape.kind = ShapeKind::cylinder;
            else
                read.fail(path + ".kind", "expected box, ramp or cylinder");
            shape.center =
                read.vector(read.member(item, "center", path), path + ".center", -4096, 4096);
            shape.size = read.vector(read.member(item, "size", path), path + ".size", 0.01F, 512);
            shape.color = read.albedo(read.member(item, "color", path), path + ".color");
            shape.yaw = read.scalar(item, "yaw", path, -10000, 10000, 0);
            if (shape.kind == ShapeKind::cylinder && std::abs(shape.size.x - shape.size.z) > 1e-5F)
                read.fail(path + ".size", "cylinder X and Z diameters must match");
            if (item.contains("material")) {
                const auto &material = item["material"];
                if (read.object(material, path + ".material", {"roughness", "metallic"})) {
                    shape.surface.roughness =
                        read.scalar(material, "roughness", path + ".material", 0, 1, 0.7F);
                    shape.surface.metallic =
                        read.scalar(material, "metallic", path + ".material", 0, 1, 0);
                }
            }
            if (item.contains("wall_jump")) {
                if (!item["wall_jump"].is_boolean())
                    read.fail(path + ".wall_jump", "expected a boolean");
                else
                    shape.wall_jump = item["wall_jump"].get<bool>();
            }
            level.shapes.push_back(shape);
        }
    }
    const auto &checkpoints = read.member(root, "checkpoints", "level");
    if (read.array(checkpoints, "level.checkpoints", 1, 64)) {
        for (std::size_t i = 0; i < checkpoints.size(); ++i) {
            const auto &item = checkpoints[i];
            const auto path = "level.checkpoints[" + std::to_string(i) + "]";
            if (!read.object(item, path, {"position", "yaw"}))
                break;
            Checkpoint checkpoint;
            checkpoint.position =
                read.vector(read.member(item, "position", path), path + ".position", -4096, 4096);
            checkpoint.yaw = read.scalar(item, "yaw", path, -10000, 10000, 0);
            if (checkpoint.position.y <= level.kill_height)
                read.fail(path + ".position", "checkpoint must be above the kill plane");
            level.checkpoints.push_back(checkpoint);
        }
    }
    const auto &lights = read.member(root, "lights", "level", false);
    if (root.contains("lights") && read.array(lights, "level.lights", 0, k_max_local_lights)) {
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const auto &item = lights[i];
            const auto path = "level.lights[" + std::to_string(i) + "]";
            if (!read.object(item, path,
                             {"position", "color", "intensity", "range", "direction", "inner_cone",
                              "outer_cone", "orbit_radius", "orbit_speed", "night_only"}))
                break;
            LightFixture light;
            light.position =
                read.vector(read.member(item, "position", path), path + ".position", -4096, 4096);
            light.color = read.vector(read.member(item, "color", path), path + ".color", 0, 1);
            light.intensity = read.scalar(item, "intensity", path, 0, 1000, 10);
            light.range = read.scalar(item, "range", path, 0.01F, 200, 10);
            if (item.contains("direction"))
                light.direction = read.vector(item["direction"], path + ".direction", -1, 1);
            if (length_squared(light.direction) < 1e-6F)
                read.fail(path + ".direction", "light direction must be nonzero");
            else
                light.direction = normalized(light.direction);
            light.inner_cone = read.scalar(item, "inner_cone", path, 0, k_pi, k_pi);
            light.outer_cone = read.scalar(item, "outer_cone", path, 0.001F, k_pi, k_pi);
            if (light.inner_cone > light.outer_cone)
                read.fail(path + ".inner_cone", "inner cone exceeds outer cone");
            light.orbit_radius = read.scalar(item, "orbit_radius", path, 0, 64, 0);
            light.orbit_speed = read.scalar(item, "orbit_speed", path, -10, 10, 0);
            if (item.contains("night_only")) {
                if (!item["night_only"].is_boolean())
                    read.fail(path + ".night_only", "expected a boolean");
                else
                    light.night_only = item["night_only"].get<bool>();
            }
            level.lights.push_back(light);
        }
    }
    const auto &zones = read.member(root, "zones", "level", false);
    if (root.contains("zones") && read.array(zones, "level.zones", 0, 32)) {
        for (std::size_t i = 0; i < zones.size(); ++i) {
            const auto &item = zones[i];
            const auto path = "level.zones[" + std::to_string(i) + "]";
            if (!read.object(item, path, {"name", "center", "radius"}))
                break;
            LevelZone zone;
            zone.name = read.name(read.member(item, "name", path), path + ".name");
            zone.center =
                read.vector(read.member(item, "center", path), path + ".center", -4096, 4096);
            zone.radius = read.scalar(item, "radius", path, 1, 200, 20);
            level.zones.push_back(zone);
        }
    }
    if (!read.error.empty())
        return std::unexpected(Error{read.error});
    return level;
}

std::expected<Level, Error> load_level(const std::filesystem::path &path) {
    const auto name = display_path(path);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return std::unexpected(Error{"Cannot open level: " + name});
    const auto size = file.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(k_max_level_bytes))
        return std::unexpected(Error{"Level is empty or exceeds 2 MiB: " + name});
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(text.data(), size);
    if (!file)
        return std::unexpected(Error{"Cannot read level: " + name});
    auto level = parse_level(text);
    if (!level)
        level.error().message = name + ": " + level.error().message;
    return level;
}

} // namespace mj::game
