#version 450
layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 world_position;
layout(set = 0, binding = 0) uniform Frame {
    mat4 view_projection;
    vec4 sun_direction;
    vec4 blob_shadow; // xyz caster feet, w radius (0 disables).
    vec4 camera_position;
} frame;
layout(push_constant) uniform Draw {
    vec4 model_rows[3];
    vec4 material; // x grid strength, y receives blob shadow.
} draw;
layout(location = 0) out vec4 out_color;

// Anti-aliased 1 m grid on the dominant face plane; fades before it can shimmer.
float grid_lines(vec3 n) {
    vec3 a = abs(n);
    vec2 uv = a.y > 0.7 ? world_position.xz : (a.x > a.z ? world_position.zy : world_position.xy);
    vec2 width = max(fwidth(uv), vec2(1.0e-4));
    vec2 distance_to_line = abs(fract(uv - 0.5) - 0.5) / width;
    float line = 1.0 - min(min(distance_to_line.x, distance_to_line.y), 1.0);
    return line * (1.0 - smoothstep(0.08, 0.3, max(width.x, width.y)));
}

// Soft disc on up-facing surfaces below the caster; fades with drop height.
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

void main() {
    vec3 n = normalize(normal);
    float diffuse = max(dot(n, frame.sun_direction.xyz), 0.0);
    vec3 lit = color * (0.28 + 0.72 * diffuse);
    lit *= 1.0 - 0.14 * draw.material.x * grid_lines(n);
    lit *= 1.0 - 0.5 * blob(n);
    // Gentle distance fill keeps the graybox legible; no baked shadows.
    float haze = 1.0 - exp(-length(world_position - frame.camera_position.xyz) * 0.006);
    lit = mix(lit, vec3(0.38, 0.49, 0.57), haze);
    out_color = vec4(lit, 1.0); // The sRGB attachment encodes linear output.
}
