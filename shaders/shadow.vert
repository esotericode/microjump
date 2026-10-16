#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec3 in_position;
void main() {
    gl_Position = frame.light_view_projection * vec4(to_world(in_position), 1.0);
}
