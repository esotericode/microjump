// Shared by every scene-pass shader; mirrors FrameUniforms / DrawConstants in renderer.cpp.
struct Light {
    vec4 position_range;      // xyz position, w range (m)
    vec4 color;               // rgb linear color * intensity
    vec4 direction_cos_outer; // xyz spot direction, w cos(outer half-angle)
    vec4 cos_inner;           // x cos(inner half-angle)
};
layout(set = 0, binding = 0) uniform Frame {
    mat4 view_projection;
    mat4 inverse_view_projection;
    mat4 light_view_projection;
    vec4 camera_position;
    vec4 sun_direction; // toward the light
    vec4 sun_color;
    vec4 sky_zenith;
    vec4 sky_horizon; // w fog density
    vec4 ground;
    vec4 blob_shadow; // xyz caster feet, w radius
    vec4 shadow;      // x texel meters, y normal offset (texels), z kernel radius, w 1/resolution
    vec4 counts;      // x local lights, y blob strength
    Light lights[16];
} frame;
layout(push_constant) uniform Draw {
    vec4 model_rows[3]; // rigid model transform rows
    vec4 material;      // x grid strength, y receives blob shadow, z emissive
    vec4 tint;
} draw;

vec3 to_world(vec3 local) {
    vec4 p = vec4(local, 1.0);
    return vec3(dot(draw.model_rows[0], p), dot(draw.model_rows[1], p), dot(draw.model_rows[2], p));
}
vec3 rotate_to_world(vec3 local) {
    return vec3(dot(draw.model_rows[0].xyz, local), dot(draw.model_rows[1].xyz, local),
                dot(draw.model_rows[2].xyz, local));
}

// Sky radiance in direction `dir`, shared by the sky pass and fog so they match.
vec3 sky_radiance(vec3 dir) {
    float up = clamp(dir.y, -1.0, 1.0);
    vec3 sky = mix(frame.sky_horizon.rgb, frame.sky_zenith.rgb, pow(max(up, 0.0), 0.55));
    return up >= 0.0 ? sky : mix(frame.sky_horizon.rgb, frame.ground.rgb, min(-up * 4.0, 1.0));
}
