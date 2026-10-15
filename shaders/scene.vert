#version 450
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(set = 0, binding = 0) uniform Frame {
    mat4 view_projection;
    vec4 sun_direction;
    vec4 blob_shadow;
    vec4 camera_position;
} frame;
// Rigid model transform as three rows, plus material parameters.
layout(push_constant) uniform Draw {
    vec4 model_rows[3];
    vec4 material;
} draw;
layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;
layout(location = 2) out vec3 world_position;
void main() {
    vec4 local = vec4(in_position, 1.0);
    world_position = vec3(dot(draw.model_rows[0], local), dot(draw.model_rows[1], local),
                          dot(draw.model_rows[2], local));
    normal = vec3(dot(draw.model_rows[0].xyz, in_normal), dot(draw.model_rows[1].xyz, in_normal),
                  dot(draw.model_rows[2].xyz, in_normal));
    color = in_color;
    gl_Position = frame.view_projection * vec4(world_position, 1.0);
}
