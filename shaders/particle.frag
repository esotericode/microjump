#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec2 local_position;
layout(location = 1) in vec3 world_position;
layout(location = 0) out vec4 out_color;

void main() {
    float radius2 = dot(local_position, local_position);
    float coverage = 1.0 - smoothstep(0.1, 1.0, radius2);
    // A hemisphere normal gives the disc a little volume without an opaque sphere.
    vec3 local_normal = vec3(local_position, sqrt(max(1.0 - radius2, 0.0)));
    vec3 n = normalize(normal_to_world(local_normal));
    vec3 sky_average = 0.5 * (frame.sky_horizon.rgb + frame.sky_zenith.rgb);
    vec3 ambient = mix(frame.ground.rgb, sky_average, n.y * 0.5 + 0.5);
    // A broad diffuse lobe catches daylight across the puff instead of making pale
    // dust disappear into the ground. It follows sun/moon intensity, never emission.
    float wrapped_diffuse = max(dot(n, normalize(frame.sun_direction.xyz)) + 0.5, 0.0) / 1.5;
    vec3 sunlight = frame.sun_color.rgb * (0.75 / 3.14159265) * wrapped_diffuse;
    vec3 lit = draw.tint.rgb * (ambient + sunlight + vec3(draw.material.z));
    vec3 to_camera = frame.camera_position.xyz - world_position;
    float distance = length(to_camera);
    vec3 toward_particle = -to_camera / max(distance, 1.0e-4);
    float fog = 1.0 - exp(-distance * frame.sky_horizon.w);
    lit = mix(lit, sky_radiance(toward_particle), fog);
    out_color = vec4(lit, draw.material.w * coverage); // Straight alpha, linear HDR.
}
