#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;
layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 world_position;
layout(location = 3) in vec2 surface; // x roughness, y metallic
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265;

// Anti-aliased 1 m grid on the dominant face plane; fades before it can shimmer.
float grid_lines(vec3 n) {
    vec3 a = abs(n);
    vec2 uv = a.y > 0.7 ? world_position.xz : (a.x > a.z ? world_position.zy : world_position.xy);
    vec2 width = max(fwidth(uv), vec2(1.0e-4));
    vec2 distance_to_line = abs(fract(uv - 0.5) - 0.5) / width;
    float line = 1.0 - min(min(distance_to_line.x, distance_to_line.y), 1.0);
    return line * (1.0 - smoothstep(0.08, 0.3, max(width.x, width.y)));
}

// Soft disc on up-facing surfaces below the player; fades with drop height.
float blob(vec3 n) {
    if (frame.blob_shadow.w <= 0.0 || draw.material.y <= 0.0 || n.y < 0.3)
        return 0.0;
    vec3 offset = world_position - frame.blob_shadow.xyz;
    float drop = -offset.y;
    if (drop < -0.05)
        return 0.0;
    float radius = frame.blob_shadow.w;
    float disc = 1.0 - smoothstep(radius * 0.55, radius, length(offset.xz));
    return disc * (1.0 - smoothstep(3.0, 16.0, drop));
}

// Percentage-closer filtering of the sun shadow map. The receiver is pushed along its normal
// by a few texels (more at grazing light) so flat surfaces do not self-shadow (acne) without
// the large depth bias that would detach shadows from their casters.
float sun_visibility(vec3 n, float n_dot_l) {
    vec3 offset = n * frame.shadow.x * frame.shadow.y * (2.0 - n_dot_l);
    vec4 clip = frame.light_view_projection * vec4(world_position + offset, 1.0);
    vec3 coord = vec3(clip.xy * 0.5 + 0.5, clip.z);
    if (coord.z >= 1.0 || coord.z <= 0.0)
        return 1.0;
    int radius = int(frame.shadow.z);
    float texel = frame.shadow.w;
    float sum = 0.0;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x)
            sum += texture(shadow_map, vec3(coord.xy + vec2(x, y) * texel, coord.z));
    }
    float lit = sum / float((2 * radius + 1) * (2 * radius + 1));
    vec2 edge = abs(coord.xy * 2.0 - 1.0); // Fade out toward the edge of the covered area.
    return mix(lit, 1.0, smoothstep(0.85, 1.0, max(edge.x, edge.y)));
}

vec3 fresnel(vec3 f0, float cos_theta) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

// Cook-Torrance microfacet specular (GGX distribution, Smith-Schlick visibility, Schlick
// Fresnel) plus energy-conserving Lambert diffuse.
vec3 shade(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 albedo, vec2 material) {
    float n_dot_l = dot(n, l);
    if (n_dot_l <= 0.0)
        return vec3(0.0);
    float roughness = clamp(material.x, 0.045, 1.0);
    float metallic = material.y;
    vec3 h = normalize(v + l);
    float n_dot_v = max(dot(n, v), 1.0e-4);
    float n_dot_h = max(dot(n, h), 0.0);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float d = n_dot_h * n_dot_h * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / (PI * d * d);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float visibility = 1.0 / (4.0 * (n_dot_v * (1.0 - k) + k) * (n_dot_l * (1.0 - k) + k));
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 f = fresnel(f0, max(dot(v, h), 0.0));
    vec3 diffuse = (1.0 - f) * (1.0 - metallic) * albedo / PI;
    return (diffuse + distribution * visibility * f) * radiance * n_dot_l;
}

vec3 local_lights(vec3 n, vec3 v, vec3 albedo, vec2 material) {
    vec3 total = vec3(0.0);
    int count = int(frame.counts.x);
    for (int i = 0; i < count; ++i) {
        Light light = frame.lights[i];
        vec3 to_light = light.position_range.xyz - world_position;
        float distance2 = max(dot(to_light, to_light), 1.0e-4);
        float distance = sqrt(distance2);
        float range = light.position_range.w;
        // Inverse-square falloff, smoothly windowed to exactly zero at the range.
        float window = clamp(1.0 - pow(distance / range, 4.0), 0.0, 1.0);
        float attenuation = window * window / max(distance2, 0.01);
        vec3 l = to_light / distance;
        float cos_outer = light.direction_cos_outer.w;
        float cone = 1.0;
        if (cos_outer > -0.999) {
            float along = dot(-l, light.direction_cos_outer.xyz);
            cone = smoothstep(cos_outer, max(light.cos_inner.x, cos_outer + 1.0e-3), along);
        }
        if (attenuation * cone > 0.0)
            total += shade(n, v, l, light.color.rgb * attenuation * cone, albedo, material);
    }
    return total;
}

void main() {
    vec3 n = normalize(normal);
    vec3 to_camera = frame.camera_position.xyz - world_position;
    float view_distance = length(to_camera);
    vec3 v = to_camera / max(view_distance, 1.0e-4);
    vec3 albedo = color * (1.0 - 0.14 * draw.material.x * grid_lines(n));
    vec2 material = surface;

    vec3 sun = normalize(frame.sun_direction.xyz);
    float n_dot_l = max(dot(n, sun), 0.0);
    vec3 lit = shade(n, v, sun, frame.sun_color.rgb, albedo, material);
    if (n_dot_l > 0.0)
        lit *= sun_visibility(n, n_dot_l);
    lit += local_lights(n, v, albedo, material);

    // Hemisphere ambient (radiance; Lambert's 1/pi cancels the hemisphere integral) and a
    // rough sky reflection so smooth surfaces still read as glossy in shade.
    vec3 sky_average = 0.5 * (frame.sky_horizon.rgb + frame.sky_zenith.rgb);
    vec3 hemisphere = mix(frame.ground.rgb, sky_average, n.y * 0.5 + 0.5);
    float n_dot_v = max(dot(n, v), 0.0);
    vec3 f0 = mix(vec3(0.04), albedo, material.y);
    vec3 reflection = sky_radiance(reflect(-v, n));
    vec3 glossy = mix(reflection, hemisphere, material.x);
    vec3 f_ambient = f0 + (max(vec3(1.0 - material.x), f0) - f0) * pow(1.0 - n_dot_v, 5.0);
    lit += hemisphere * albedo * (1.0 - material.y) * (1.0 - f_ambient) + glossy * f_ambient;

    lit *= 1.0 - frame.counts.y * blob(n);
    lit += albedo * draw.material.z; // Emissive (light bulbs).
    float fog = 1.0 - exp(-view_distance * frame.sky_horizon.w);
    lit = mix(lit, sky_radiance(-v), fog);
    out_color = vec4(lit, 1.0); // Linear HDR; the tone-map pass produces display values.
}
