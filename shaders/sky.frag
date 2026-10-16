#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;
void main() {
    // Unproject a point on the near plane (reverse-Z depth 1) to get the view ray.
    vec4 near_point = frame.inverse_view_projection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(near_point.xyz / near_point.w - frame.camera_position.xyz);
    vec3 color = sky_radiance(dir);
    float sun = max(dot(dir, frame.sun_direction.xyz), 0.0);
    color += frame.sun_color.rgb * (pow(sun, 1500.0) * 40.0 + pow(sun, 12.0) * 0.06);
    out_color = vec4(color, 1.0);
}
