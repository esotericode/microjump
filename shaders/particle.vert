#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) out vec2 local_position;
layout(location = 1) out vec3 world_position;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1),
                                  vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    local_position = corners[gl_VertexIndex];
    world_position = to_world(vec3(local_position * draw.material.x, 0.0));
    gl_Position = frame.view_projection * vec4(world_position, 1.0);
}
