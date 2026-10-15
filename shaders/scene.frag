#version 450
layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 world_position;
layout(location = 0) out vec4 out_color;
void main() {
    float diffuse = max(dot(normalize(normal), normalize(vec3(-0.4, 0.85, 0.3))), 0.0);
    vec3 lit = color * (0.28 + 0.72 * diffuse);
    // Gentle distance fill keeps the original graybox legible; no baked shadows.
    float haze = 1.0 - exp(-length(world_position.xz) * 0.008);
    lit = mix(lit, vec3(0.38, 0.49, 0.57), haze);
    out_color = vec4(lit, 1.0); // The sRGB attachment encodes linear output.
}
