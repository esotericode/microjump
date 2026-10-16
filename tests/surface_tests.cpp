#include "test_framework.hpp"

#include <level_io.hpp>

#include <array>
#include <cmath>
#include <map>
#include <string_view>
#include <tuple>
#include <vector>

using mj::Vec3;

namespace {
// One front face of a level, remembering which shape drew it.
struct Face {
    std::array<Vec3, 3> corner;
    Vec3 normal;
    std::size_t shape = 0;
};
using Polygon = std::vector<std::array<float, 2>>;

float signed_area(const Polygon &polygon) {
    float sum = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto &a = polygon[i];
        const auto &b = polygon[(i + 1) % polygon.size()];
        sum += a[0] * b[1] - a[1] * b[0];
    }
    return sum * 0.5F;
}

// Sutherland-Hodgman: keeps the part of `subject` left of the edge from `a` to `b`.
Polygon clip(const Polygon &subject, std::array<float, 2> a, std::array<float, 2> b) {
    const auto side = [&](std::array<float, 2> p) {
        return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
    };
    Polygon kept;
    for (std::size_t i = 0; i < subject.size(); ++i) {
        const auto p = subject[i];
        const auto q = subject[(i + 1) % subject.size()];
        const float sp = side(p);
        const float sq = side(q);
        if (sp >= 0)
            kept.push_back(p);
        if ((sp >= 0) != (sq >= 0)) {
            const float t = sp / (sp - sq);
            kept.push_back({p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t});
        }
    }
    return kept;
}

// Whether `p` is inside a shape's solid volume, a little shrunk so surfaces don't count.
bool inside(const mj::game::Shape &shape, Vec3 p) {
    const Vec3 d = p - shape.center;
    const float c = std::cos(shape.yaw);
    const float s = std::sin(shape.yaw);
    const Vec3 local{d.x * c - d.z * s, d.y, d.x * s + d.z * c}; // Undo the generators' yaw.
    const Vec3 half = shape.size * 0.5F;
    const auto ratio = [](float value, float limit) { return value * value / (limit * limit); };
    switch (shape.kind) {
    case mj::game::ShapeKind::box:
        return std::abs(local.x) < half.x && std::abs(local.y) < half.y &&
               std::abs(local.z) < half.z;
    case mj::game::ShapeKind::ramp:
        return std::abs(local.x) < half.x && std::abs(local.z) < half.z && local.y > -half.y &&
               local.y < -half.y + half.y * (half.z - local.z) / half.z;
    case mj::game::ShapeKind::cylinder:
        return std::abs(d.y) < half.y && ratio(d.x, half.x) + ratio(d.z, half.x) < 1;
    case mj::game::ShapeKind::ellipsoid:
        return ratio(local.x, half.x) + ratio(local.y, half.y) + ratio(local.z, half.z) < 0.97F;
    case mj::game::ShapeKind::cone: {
        if (std::abs(local.y) >= half.y)
            return false;
        float along = (local.y + half.y) / (2 * half.y); // 0 at the wide end.
        if (shape.tip_down)
            along = 1 - along;
        const float scale = 1 - along * (1 - shape.taper);
        return ratio(local.x, half.x * scale) + ratio(local.z, half.z * scale) < 0.9F;
    }
    }
    return false;
}

struct Fight {
    std::size_t first = 0;
    std::size_t second = 0;
    float area = 0;
};

// Visible z-fighting: front faces of two differently coloured shapes in the same plane,
// facing the same way and overlapping, where nothing solid covers the overlap. Identical
// surfaces in one plane shade identically, so they cannot flicker.
std::vector<Fight> z_fights(const mj::game::Level &level) {
    std::vector<Face> faces;
    for (std::size_t index = 0; index < level.shapes.size(); ++index) {
        mj::game::Level alone = level;
        alone.shapes = {level.shapes[index]};
        alone.shapes.front().solid = true;
        const auto mesh = mj::game::build_geometry(alone).mesh;
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            const std::array<Vec3, 3> corner{mesh.vertices[i].position,
                                             mesh.vertices[i + 1].position,
                                             mesh.vertices[i + 2].position};
            const Vec3 normal = mj::cross(corner[1] - corner[0], corner[2] - corner[0]);
            if (mj::length_squared(normal) > 1e-10F)
                faces.push_back({corner, mj::normalized(normal), index});
        }
    }
    // Bucket faces by plane, so only faces that could share one are compared.
    std::map<std::tuple<long, long, long, long>, std::vector<std::size_t>> planes;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto &face = faces[i];
        const auto key = [](float value) { return std::lround(value * 200); };
        planes[{key(face.normal.x), key(face.normal.y), key(face.normal.z),
                key(mj::dot(face.normal, face.corner[0]))}]
            .push_back(i);
    }
    std::map<std::pair<std::size_t, std::size_t>, float> fights;
    for (const auto &[plane, members] : planes) {
        for (std::size_t x = 0; x < members.size(); ++x) {
            for (std::size_t y = x + 1; y < members.size(); ++y) {
                const auto &f = faces[members[x]];
                const auto &g = faces[members[y]];
                const auto &a = level.shapes[f.shape];
                const auto &b = level.shapes[g.shape];
                if (f.shape == g.shape || mj::dot(f.normal, g.normal) < 0.9999F ||
                    std::abs(mj::dot(f.normal, g.corner[0] - f.corner[0])) > 2e-3F ||
                    (mj::length(a.color - b.color) < 1e-6F &&
                     a.surface.roughness == b.surface.roughness &&
                     a.surface.metallic == b.surface.metallic))
                    continue;
                const Vec3 u = mj::normalized(mj::cross(
                    f.normal, std::abs(f.normal.y) < 0.9F ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
                const Vec3 w = mj::cross(f.normal, u);
                const auto flat = [&](const std::array<Vec3, 3> &corner) {
                    Polygon polygon;
                    for (const auto &point : corner)
                        polygon.push_back({mj::dot(point, u), mj::dot(point, w)});
                    if (signed_area(polygon) < 0)
                        std::swap(polygon[1], polygon[2]);
                    return polygon;
                };
                Polygon overlap = flat(f.corner);
                const Polygon other = flat(g.corner);
                for (std::size_t k = 0; k < 3 && !overlap.empty(); ++k)
                    overlap = clip(overlap, other[k], other[(k + 1) % 3]);
                const float area = overlap.size() >= 3 ? std::abs(signed_area(overlap)) : 0;
                if (area < 1e-4F)
                    continue;
                std::array<float, 2> middle{};
                for (const auto &point : overlap) {
                    middle[0] += point[0] / static_cast<float>(overlap.size());
                    middle[1] += point[1] / static_cast<float>(overlap.size());
                }
                // Just in front of the overlap: buried faces (under the ground) can't be seen.
                const Vec3 front = f.normal * mj::dot(f.normal, f.corner[0]) + u * middle[0] +
                                   w * middle[1] + f.normal * 0.01F;
                bool covered = false;
                for (const auto &shape : level.shapes)
                    covered = covered || (shape.solid && inside(shape, front));
                if (!covered)
                    fights[{f.shape, g.shape}] += area;
            }
        }
    }
    std::vector<Fight> result;
    for (const auto &[pair, area] : fights)
        result.push_back({pair.first, pair.second, area});
    return result;
}
} // namespace

TEST_CASE("the z-fighting check sees coplanar overlaps that show, and ignores buried ones") {
    const auto level = mj::game::parse_level(R"({
      "version": 1, "name": "Planes",
      "shapes": [{"kind":"box", "center":[0,-0.5,0], "size":[20,1,20], "color":"#808080"},
                 {"kind":"box", "center":[0,0.5,0], "size":[2,1,2], "color":"#ff0000"},
                 {"kind":"box", "center":[0.5,0.5,0], "size":[2,1,2], "color":"#00ff00"},
                 {"kind":"box", "center":[5,0.5,0], "size":[2,1,2], "color":"#ff0000"},
                 {"kind":"box", "center":[5.5,0.5,0], "size":[2,1,2], "color":"#ff0000"}],
      "checkpoints": [{"position":[0,0.01,6]}]})");
    REQUIRE(level);
    // The red and green boxes share their top, front and back faces where they overlap,
    // and their bottoms are buried in the floor. Same-coloured boxes can't flicker.
    const auto fights = z_fights(*level);
    REQUIRE(fights.size() == 1);
    CHECK(fights.front().first == 1);
    CHECK(fights.front().second == 2);
    CHECK(fights.front().area ==
          doctest::Approx(3 + 1.5F + 1.5F).epsilon(0.01)); // Top, front, back.
}

TEST_CASE("shipped levels have no visible z-fighting") {
    for (const std::string_view name :
         {"playground.json", "lantern-isles.json", "movement-yard.json"}) {
        CAPTURE(name);
        const auto level = mj::game::load_level(std::filesystem::path(MJ_TEST_LEVELS) / name);
        REQUIRE(level);
        for (const auto &fight : z_fights(*level)) {
            const auto &a = level->shapes[fight.first];
            const auto &b = level->shapes[fight.second];
            CAPTURE(fight.first);
            CAPTURE(a.center);
            CAPTURE(fight.second);
            CAPTURE(b.center);
            CHECK(fight.area == 0.0F); // Fails with the two shapes that z-fight.
        }
    }
}
