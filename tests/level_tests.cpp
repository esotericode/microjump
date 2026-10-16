#include "test_framework.hpp"

#include <level_io.hpp>
#include <simulation.hpp>

#include <fstream>
#include <string>
#include <utility>

namespace {
constexpr std::string_view k_level = R"({
  "version": 1, "name": "Test yard", "kill_height": -25,
  "shapes": [{"kind":"box", "center":[0,-0.5,0], "size":[20,1,20], "color":"#808080",
              "material":{"roughness":0.1,"metallic":0.9}}],
  "checkpoints": [{"position":[0,0.01,0]}],
  "lights": [{"position":[0,3,0],"color":[1,0.5,0],"direction":[0,-0.5,0],"range":12}],
  "zones": [{"name":"Plaza","center":[0,0,0],"radius":8}]
})";
std::string changed(std::string_view from, std::string_view to) {
    std::string text(k_level);
    const auto offset = text.find(from);
    REQUIRE(offset != std::string::npos);
    text.replace(offset, from.size(), to);
    return text;
}
std::filesystem::path asset(std::string_view name) {
    return std::filesystem::path(MJ_TEST_LEVELS) / name;
}
constexpr float k_dt = 1.0F / 120;
} // namespace

TEST_CASE("versioned levels preserve units, linear colors, materials and collision") {
    const auto level = mj::game::parse_level(k_level);
    REQUIRE(level);
    CHECK(level->name == "Test yard");
    REQUIRE(level->shapes.size() == 1);
    const auto &shape = level->shapes.front();
    CHECK(shape.color.x == doctest::Approx(0.2158605F));
    CHECK_FALSE(shape.wall_jump);
    CHECK(shape.surface.roughness == doctest::Approx(0.1F));
    CHECK(shape.surface.metallic == doctest::Approx(0.9F));
    REQUIRE(level->lights.size() == 1);
    CHECK(level->lights[0].direction.y == doctest::Approx(-1.0F));
    CHECK(mj::game::zone_at(*level, {3, 20, 4}) == "Plaza");
    CHECK(mj::game::zone_at(*level, {9, 0, 0}) == "Connections");
    const auto geometry = mj::game::build_geometry(*level);
    const auto ground = geometry.collision.raycast({{4, 5, 3}, {0, -1, 0}, 10});
    REQUIRE(ground);
    CHECK(ground->distance == doctest::Approx(5.0F));
    CHECK(ground->normal.y == doctest::Approx(1.0F));
}

TEST_CASE("level parsing rejects malformed versions, placement, material and light data") {
    CHECK_FALSE(mj::game::parse_level("{"));
    CHECK_FALSE(mj::game::parse_level("[]"));
    for (const auto &[from, to] :
         {std::pair{"\"version\": 1", "\"version\": 2"},
          {"\"version\": 1", "\"version\": 1.5"},
          {"\"name\": \"Test yard\"", "\"name\": null"},
          {"\"name\": \"Test yard\"", "\"name\": \"\""},
          {"\"name\": \"Test yard\"", "\"name\": \"Two\\nlines\""},
          {"\"name\": \"Test yard\"", "\"name\":\"First\",\"name\":\"Second\""},
          {"\"name\": \"Test yard\"", "\"typo\": \"Test yard\""},
          {"\"kind\":\"box\"", "\"kind\":\"mesh\""},
          {"\"kind\":\"box\"", "\"kind\":\"box\",\"wall_jump\":1"},
          {"\"center\":[0,-0.5,0]", "\"center\":[0,0]"},
          {"\"center\":[0,-0.5,0]", "\"center\":[1e100,0,0]"},
          {"\"size\":[20,1,20]", "\"size\":[0,1,20]"},
          {"\"size\":[20,1,20]", "\"size\":[20,-1,20]"},
          {"\"color\":\"#808080\"", "\"color\":\"#gggggg\""},
          {"\"roughness\":0.1", "\"roughness\":2"},
          {"\"metallic\":0.9", "\"metallic\":-0.1"},
          {"\"material\":{", "\"material\":{\"unknown\":1,"},
          {"\"position\":[0,0.01,0]", "\"position\":[0,-30,0]"},
          {"\"range\":12", "\"range\":0"},
          {"\"range\":12", "\"range\":1e999"},
          {"\"range\":12", "\"range\":12,\"night_only\":\"yes\""},
          {"\"range\":12", "\"range\":12,\"inner_cone\":1,\"outer_cone\":0.5"},
          {"\"direction\":[0,-0.5,0]", "\"direction\":[0,0,0]"},
          {"\"radius\":8", "\"radius\":-1"}}) {
        CAPTURE(from);
        CAPTURE(to);
        CHECK_FALSE(mj::game::parse_level(changed(from, to)));
    }
    CHECK(mj::game::parse_level(changed("\"kind\":\"box\"", "\"kind\":\"cylinder\"")).has_value());
    const auto cylinder = changed("\"kind\":\"box\"", "\"kind\":\"cylinder\"");
    auto unequal = cylinder;
    unequal.replace(unequal.find("[20,1,20]"), 9, "[20,1,10]");
    CHECK_FALSE(mj::game::parse_level(unequal));
    CHECK_FALSE(mj::game::parse_level(std::string(33, '[') + std::string(33, ']')));
    CHECK_FALSE(mj::game::parse_level(std::string(2 * 1024 * 1024 + 1, ' ')));
}

TEST_CASE("JSON yard preserves the recorded M2 movement outcome") {
    const auto level = mj::game::load_level(asset("movement-yard.json"));
    INFO((level ? "loaded" : level.error().message));
    REQUIRE(level);
    const auto original = mj::game::make_movement_yard();
    REQUIRE(level->shapes.size() == original.shapes.size());
    REQUIRE(level->checkpoints.size() == original.checkpoints.size());
    REQUIRE(level->lights.size() == original.lights.size());
    for (std::size_t i = 0; i < original.shapes.size(); ++i) {
        const auto &a = level->shapes[i];
        const auto &b = original.shapes[i];
        CHECK(a.kind == b.kind);
        CHECK(mj::length(a.center - b.center) < 1e-5F);
        CHECK(mj::length(a.size - b.size) < 1e-5F);
        CHECK(mj::length(a.color - b.color) < 1e-6F);
        CHECK(a.yaw == b.yaw);
        CHECK(a.surface.roughness == b.surface.roughness);
        CHECK(a.surface.metallic == b.surface.metallic);
    }
    const auto geometry = mj::game::build_geometry(*level);
    const auto input =
        mj::load_replay(std::filesystem::path(MJ_TEST_REPLAYS) / "m2-input-smoke.mjr");
    REQUIRE(input);
    mj::game::Simulation simulation(*level, geometry.collision);
    for (const auto &tick : *input)
        simulation.tick(tick, k_dt);
    const auto &state = simulation.player.current();
    CHECK(state.grounded);
    CHECK(state.position.x == doctest::Approx(8.37801552F).epsilon(1e-4));
    CHECK(state.position.z == doctest::Approx(-12.5603285F).epsilon(1e-4));
    CHECK(simulation.respawns() == 0);
}

TEST_CASE("playground checkpoints settle safely and its sprint and stairs are traversable") {
    const auto level = mj::game::load_level(asset("playground.json"));
    INFO((level ? "loaded" : level.error().message));
    REQUIRE(level);
    CHECK(level->shapes.front().size.x == doctest::Approx(230.0F));
    CHECK(level->shapes.front().size.z == doctest::Approx(230.0F));
    CHECK(level->checkpoints.size() >= 8);
    CHECK(level->lights.size() <= mj::k_max_local_lights);
    CHECK(level->zones.size() == 7);
    const auto geometry = mj::game::build_geometry(*level);
    for (std::size_t i = 0; i < level->checkpoints.size(); ++i) {
        CAPTURE(i);
        mj::game::Simulation simulation(*level, geometry.collision);
        simulation.player.place(level->checkpoints[i].position, level->checkpoints[i].yaw);
        for (int tick = 0; tick < 60; ++tick)
            simulation.tick({}, k_dt);
        CHECK(simulation.player.current().grounded);
        CHECK(simulation.active_checkpoint() == i);
        CHECK(simulation.respawns() == 0);
    }
    mj::game::Simulation sprint(*level, geometry.collision);
    sprint.player.place({7, 0.07F, -5}, 0);
    for (int tick = 0; tick < 1000; ++tick)
        sprint.tick({0, 1, 0, -0.32F, 0}, k_dt);
    CHECK(sprint.player.current().position.z < -75.0F);
    CHECK(sprint.player.current().grounded);

    mj::game::Simulation stairs(*level, geometry.collision);
    stairs.player.place({63, 0.005F, 28}, mj::k_pi);
    for (int tick = 0; tick < 600; ++tick)
        stairs.tick({0, 0.4F, mj::k_pi, -0.32F, 0}, k_dt);
    CHECK(stairs.player.current().grounded);
    CHECK(stairs.player.current().position.y > 5.7F);
    CHECK(stairs.respawns() == 0);
}

TEST_CASE("level files support Unicode paths and report missing or invalid files") {
    const auto path = std::filesystem::path(u8"level-test-\u96ea.json");
    {
        std::ofstream file(path, std::ios::binary);
        file << k_level;
    }
    const auto level = mj::game::load_level(path);
    CHECK(level.has_value());
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "{";
    }
    CHECK_FALSE(mj::game::load_level(path));
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK_FALSE(mj::game::load_level(path));
}

TEST_CASE("wall jump tags survive generated collision geometry and default to false") {
    const auto level =
        mj::game::parse_level(changed("\"kind\":\"box\"", "\"kind\":\"box\",\"wall_jump\":true"));
    REQUIRE(level);
    CHECK(level->shapes.front().wall_jump);
    const auto geometry = mj::game::build_geometry(*level);
    for (const auto &triangle : geometry.collision.triangles())
        CHECK(triangle.wall_jump);
}

TEST_CASE("ellipsoids, cones, drawn-only shapes, titles and atmosphere load with defaults") {
    const auto plain = mj::game::parse_level(k_level);
    REQUIRE(plain);
    CHECK(plain->title == "Test yard");
    CHECK(plain->portals.empty());
    CHECK(plain->atmosphere.time_of_day == doctest::Approx(14.5F));
    CHECK(plain->atmosphere.fog == doctest::Approx(1));
    CHECK(plain->atmosphere.grid);
    CHECK_FALSE(plain->atmosphere.sky_below);
    CHECK(plain->shapes.front().solid);

    const auto level = mj::game::parse_level(changed("\"zones\"",
                                                     R"("title": "The Test Yard",
           "atmosphere": {"time_of_day": 17, "fog": 0.5, "grid": false, "sky_below": true},
           "zones")"));
    REQUIRE(level);
    CHECK(level->title == "The Test Yard");
    CHECK(level->atmosphere.time_of_day == doctest::Approx(17));
    CHECK(level->atmosphere.fog == doctest::Approx(0.5F));
    CHECK_FALSE(level->atmosphere.grid);
    CHECK(level->atmosphere.sky_below);

    const auto shapes = mj::game::parse_level(
        changed(R"("shapes": [)",
                R"("shapes": [{"kind":"cone", "center":[0,-3,0], "size":[6,4,6], "color":"#808080",
                       "tip":"down", "taper":0.2},
                      {"kind":"ellipsoid", "center":[0,6,0], "size":[40,8,40],
                       "color":"#ffffff", "solid":false},)"));
    REQUIRE(shapes);
    REQUIRE(shapes->shapes.size() == 3);
    const auto &cone = shapes->shapes[0];
    CHECK(cone.kind == mj::game::ShapeKind::cone);
    CHECK(cone.tip_down);
    CHECK(cone.taper == doctest::Approx(0.2F));
    const auto &cloud = shapes->shapes[1];
    CHECK(cloud.kind == mj::game::ShapeKind::ellipsoid);
    CHECK_FALSE(cloud.solid);
    // Drawn-only shapes render but never collide: a cloud overhead casts no ray hit.
    const auto geometry = mj::game::build_geometry(*shapes);
    CHECK(geometry.collision.triangles().size() < geometry.mesh.vertices.size() / 3);
    CHECK_FALSE(geometry.collision.raycast({{0, 20, 0}, {0, -1, 0}, 15}));
    const auto floor = geometry.collision.raycast({{0, 1, 0}, {0, -1, 0}, 5});
    REQUIRE(floor);
    CHECK(floor->point.y == doctest::Approx(0).epsilon(1e-4));
}

TEST_CASE("level parsing rejects malformed shapes, portals and atmosphere") {
    for (const auto &[from, to] :
         {std::pair{"\"kind\":\"box\"", "\"kind\":\"box\",\"taper\":0.5"},
          {"\"kind\":\"box\"", "\"kind\":\"box\",\"tip\":\"down\""},
          {"\"kind\":\"box\"", "\"kind\":\"cone\",\"tip\":\"sideways\""},
          {"\"kind\":\"box\"", "\"kind\":\"cone\",\"taper\":1.5"},
          {"\"kind\":\"box\"", "\"kind\":\"box\",\"solid\":\"no\""},
          {"\"kind\":\"box\"", "\"kind\":\"box\",\"solid\":false"}, // Nothing left to stand on.
          {"\"kind\":\"box\"", "\"kind\":\"box\",\"wall_jump\":true,\"solid\":false"},
          {"\"zones\"", "\"title\":\"\",\"zones\""},
          {"\"zones\"", "\"atmosphere\":{\"time_of_day\":25},\"zones\""},
          {"\"zones\"", "\"atmosphere\":{\"fog\":-1},\"zones\""},
          {"\"zones\"", "\"atmosphere\":{\"grid\":1},\"zones\""},
          {"\"zones\"", "\"atmosphere\":{\"weather\":\"rain\"},\"zones\""},
          {"\"zones\"", "\"atmosphere\":[],\"zones\""}}) {
        CAPTURE(from);
        CAPTURE(to);
        CHECK_FALSE(mj::game::parse_level(changed(from, to)));
    }
    const std::string gate =
        R"({"name":"Gate","position":[0,1.5,0],"level":"other.json","checkpoint":1})";
    const auto with_portal = [](std::string_view portal) {
        return changed("\"zones\"", "\"portals\":[" + std::string(portal) + "],\"zones\"");
    };
    const auto valid = mj::game::parse_level(with_portal(gate));
    REQUIRE(valid);
    REQUIRE(valid->portals.size() == 1);
    CHECK(valid->portals.front().level == "other.json");
    CHECK(valid->portals.front().checkpoint == 1);
    CHECK(valid->portals.front().radius == doctest::Approx(1.2F));
    for (const auto &[from, to] : {std::pair{"\"other.json\"", "\"../other.json\""},
                                   {"\"other.json\"", "\"levels/other.json\""},
                                   {"\"other.json\"", "\"C:other.json\""},
                                   {"\"other.json\"", "\"other.txt\""},
                                   {"\"other.json\"", "\".json\""},
                                   {"\"other.json\"", "7"},
                                   {"\"checkpoint\":1", "\"checkpoint\":-1"},
                                   {"\"checkpoint\":1", "\"checkpoint\":1.5"},
                                   {"\"checkpoint\":1", "\"checkpoint\":64"},
                                   {"[0,1.5,0]", "[0,-40,0]"},
                                   {"\"name\":\"Gate\",", ""},
                                   {"\"checkpoint\":1", "\"checkpoint\":1,\"radius\":9"},
                                   {"\"checkpoint\":1", "\"checkpoint\":1,\"colour\":1"}}) {
        CAPTURE(to);
        auto portal = gate;
        portal.replace(portal.find(from), std::string_view(from).size(), to);
        CHECK_FALSE(mj::game::parse_level(with_portal(portal)));
    }
}

TEST_CASE("stepping into a portal is reported once, and travel keeps a found spark") {
    auto text = changed("\"zones\"",
                        R"("portals": [{"name":"Gate","position":[0,1.2,-4],"radius":1,
                                        "level":"b.json"}],
                           "collectibles": [{"name":"Near","position":[0,1,-2]},
                                            {"name":"Far","position":[8,1,8]}],
                           "zones")");
    text.replace(text.find(R"("checkpoints": [{"position":[0,0.01,0]}])"),
                 std::string_view(R"("checkpoints": [{"position":[0,0.01,0]}])").size(),
                 R"("checkpoints": [{"position":[0,0.01,0]}, {"position":[0,0.01,-4.5],"yaw":1}])");
    const auto yard = mj::game::parse_level(text);
    REQUIRE(yard);
    const auto geometry = mj::game::build_geometry(*yard);

    // Arriving at a checkpoint starts there and respawns there.
    mj::game::Simulation arrival(*yard, geometry.collision, 1);
    CHECK(arrival.active_checkpoint() == 1);
    CHECK(arrival.player.current().position.z == doctest::Approx(-4.5F));
    CHECK(arrival.player.current().facing == doctest::Approx(1));
    mj::game::Simulation clamped(*yard, geometry.collision, 40);
    CHECK(clamped.active_checkpoint() == 1);
    // Standing inside the gate on arrival does not send you straight back.
    CHECK_FALSE(arrival.portal_entered());
    for (int tick = 0; tick < 30; ++tick) {
        arrival.tick({}, k_dt);
        CHECK_FALSE(arrival.portal_entered());
    }

    mj::game::Simulation simulation(*yard, geometry.collision);
    int entries = 0;
    for (int tick = 0; tick < 200; ++tick) {
        simulation.tick({0, 0.5F, 0, 0, 0}, k_dt); // Walk north through the spark and gate.
        if (simulation.portal_entered()) {
            CHECK(*simulation.portal_entered() == 0);
            ++entries;
        }
    }
    CHECK(entries == 1);
    CHECK(simulation.collectible_collected(0));
    CHECK(simulation.player.current().position.z < -6.0F); // The gate is not a wall.

    // A hunt carries over a trip away and back; a different spark count starts fresh.
    mj::game::Simulation back(*yard, geometry.collision);
    back.restore_collected(simulation.collected());
    CHECK(back.collected_count() == 1);
    CHECK(back.collectible_collected(0));
    CHECK_FALSE(back.collectible_collected(1));
    back.restore_collected({true, true, true});
    CHECK(back.collected_count() == 1);
}
