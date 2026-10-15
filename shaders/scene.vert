#version 450
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(push_constant) uniform Camera { mat4 view_projection; } camera;
layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;
layout(location = 2) out vec3 world_position;
void main() {
    gl_Position = camera.view_projection * vec4(in_position, 1.0);
    normal = in_normal;
    color = in_color;
    world_position = in_position;
}
