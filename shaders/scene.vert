#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_surface;
layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;
layout(location = 2) out vec3 world_position;
layout(location = 3) out vec2 surface;
void main() {
    world_position = to_world(in_position);
    normal = normal_to_world(in_normal);
    color = in_color * draw.tint.rgb;
    surface = in_surface;
    gl_Position = frame.view_projection * vec4(world_position, 1.0);
}
