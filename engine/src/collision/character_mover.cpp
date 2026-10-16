#include <mj/collision/character_mover.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace mj {
namespace {

enum class SlideMode : std::uint8_t { ground, air };

struct Slider {
    const CollisionWorld &world;
    const MoveSettings &settings;
    Capsule shape;
    MoveResult result{};

    [[nodiscard]] Capsule at(Vec3 base) const {
        Capsule moved = shape;
        moved.base = base;
        return moved;
    }
    [[nodiscard]] std::optional<SweepHit> sweep(Vec3 base, Vec3 displacement) const {
        return world.sweep({at(base), displacement, settings.skin});
    }

    // Collide and slide. Walkable contacts keep the horizontal motion and follow the surface;
    // other contacts remove the blocked component. In ground mode those contacts are treated as
    // vertical walls so steep slopes cannot be climbed by sliding.
    Vec3 slide(Vec3 base, Vec3 delta, SlideMode mode) {
        const Vec3 original = delta;
        std::array<Vec3, 3> walls{};
        std::size_t wall_count = 0;
        auto respect_walls = [&](Vec3 motion) {
            for (std::size_t i = 0; i < wall_count; ++i) {
                const float into = dot(motion, walls[i]);
                if (into < 0)
                    motion -= walls[i] * into;
            }
            return motion;
        };
        for (int iteration = 0; iteration < settings.max_slides; ++iteration) {
            if (length_squared(delta) < 1.0e-12F)
                break;
            const auto hit = sweep(base, delta);
            if (!hit) {
                base += delta;
                break;
            }
            base += delta * hit->fraction;
            const Vec3 remaining = delta * (1 - hit->fraction);
            Vec3 normal = hit->normal;
            if (is_walkable(normal, settings) && (mode == SlideMode::ground || remaining.y <= 0)) {
                result.grounded = true;
                result.ground_normal = normal;
                result.ground_face_normal = hit->face_normal;
                const Vec3 along = respect_walls(horizontal(remaining));
                delta = along + Vec3{0, -(normal.x * along.x + normal.z * along.z) / normal.y, 0};
            } else {
                const Vec3 flat = horizontal(normal);
                const bool steep_floor = normal.y > 0 && length_squared(flat) > 1.0e-6F;
                if (normal.y < -0.3F) {
                    result.hit_ceiling = true;
                } else if (std::abs(normal.y) < 0.7F) {
                    result.hit_wall = true;
                    result.wall_normal = normal;
                    if (world.triangles()[hit->triangle].wall_jump &&
                        std::abs(hit->face_normal.y) < 0.1F && dot(normal, hit->face_normal) > 0.9F)
                        result.jump_wall_normal = normalized(horizontal(hit->face_normal));
                }
                if (steep_floor || (mode == SlideMode::ground && length_squared(flat) > 1.0e-6F)) {
                    // Too steep to stand on: horizontal motion treats it as a vertical wall, so
                    // running into it can never climb it. Vertical motion slides along it.
                    const Vec3 wall = normalized(flat);
                    const Vec3 sideways = horizontal(remaining) - wall * dot(remaining, wall);
                    const Vec3 vertical{0, mode == SlideMode::ground ? 0.0F : remaining.y, 0};
                    delta = sideways + vertical - normal * dot(vertical, normal);
                    normal = wall;
                } else {
                    delta = remaining - normal * dot(remaining, normal);
                }
                if (wall_count == 1 && dot(delta, walls[0]) < 0) {
                    const Vec3 crease = normalized(cross(walls[0], normal));
                    delta = crease * dot(delta, crease);
                } else if (wall_count >= 2) {
                    delta = {};
                }
                if (wall_count < walls.size())
                    walls[wall_count++] = normal;
            }
            // Never turn back against the requested motion; that is where corner jitter starts.
            const bool reversed = mode == SlideMode::ground
                                      ? dot(horizontal(delta), horizontal(original)) <= 0
                                      : dot(delta, original) <= 0;
            if (reversed)
                break;
        }
        return base;
    }

    // Probes down `distance`; lands on walkable ground. A step is judged by the face it lands
    // on (the capsule first touches a step at its front edge), which must be no higher than
    // `step_top` so the rounded bottom cannot ratchet up the edge of a taller block.
    bool snap_down(Vec3 &base, float distance, std::optional<float> step_top = std::nullopt) {
        // Disabling ledge snap still needs a contact probe across the collision skin.
        distance = std::max(distance, settings.skin * 2);
        const auto hit = sweep(base, {0, -distance, 0});
        if (!hit)
            return false;
        if (step_top ? !is_walkable(hit->face_normal, settings) || hit->point.y > *step_top
                     : !is_walkable(hit->normal, settings))
            return false;
        base.y -= distance * hit->fraction;
        result.grounded = true;
        result.ground_normal = hit->normal;
        result.ground_face_normal = hit->face_normal;
        return true;
    }
};

float horizontal_progress(Vec3 from, Vec3 to, Vec3 direction) {
    return dot(horizontal(to - from), direction);
}

} // namespace

MoveResult move_character(const CollisionWorld &world, const MoveSettings &settings,
                          const MoveRequest &request) {
    Capsule shape = request.shape;
    shape.base += world.depenetration(shape, settings.skin);
    const Vec3 start = shape.base;

    if (!request.grounded || request.displacement.y > 0) {
        Slider slider{world, settings, shape};
        Vec3 base = slider.slide(start, request.displacement, SlideMode::air);
        if (!slider.result.grounded && request.displacement.y <= 0)
            slider.snap_down(base, settings.skin * 2);
        slider.result.base = base;
        return slider.result;
    }

    const Vec3 flat = horizontal(request.displacement);
    const Vec3 direction = normalized(flat);
    Slider plain{world, settings, shape};
    Vec3 base = plain.slide(start, flat, SlideMode::ground);
    if (!plain.snap_down(base, settings.ground_snap))
        plain.result.grounded = false;
    plain.result.base = base;
    const float plain_progress = horizontal_progress(start, base, direction);
    if (!plain.result.hit_wall || plain_progress >= length(flat) - 1.0e-5F)
        return plain.result;

    // Blocked: try stepping over the obstacle, and keep it only if it lands on walkable ground
    // further along than the plain slide got.
    Slider step{world, settings, shape};
    const auto up = step.sweep(start, {0, settings.step_height, 0});
    const float climb = settings.step_height * (up ? up->fraction : 1.0F);
    Vec3 raised = step.slide(start + Vec3{0, climb, 0}, flat, SlideMode::ground);
    step.result.grounded = false;
    if (!step.snap_down(raised, climb + settings.ground_snap,
                        start.y + settings.step_height + settings.skin))
        return plain.result;
    if (horizontal_progress(start, raised, direction) <= plain_progress + 1.0e-4F)
        return plain.result;
    step.result.base = raised;
    step.result.stepped = true;
    return step.result;
}

} // namespace mj
