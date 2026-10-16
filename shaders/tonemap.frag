#version 450
layout(set = 0, binding = 0) uniform sampler2D hdr;
layout(push_constant) uniform Post {
    vec4 exposure; // x exposure multiplier
} post;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

// Khronos PBR Neutral tone mapping: keeps base colors faithful below ~0.8 and rolls highlights
// off smoothly toward white. Output is linear; the sRGB attachment encodes it.
vec3 neutral(vec3 color) {
    const float start = 0.8 - 0.04;
    const float desaturation = 0.15;
    float low = min(color.r, min(color.g, color.b));
    float offset = low < 0.08 ? low - 6.25 * low * low : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < start)
        return color;
    const float d = 1.0 - start;
    float new_peak = 1.0 - d * d / (peak + d - start);
    color *= new_peak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - new_peak) + 1.0);
    return mix(color, vec3(new_peak), g);
}

void main() {
    vec3 color = texelFetch(hdr, ivec2(gl_FragCoord.xy), 0).rgb * post.exposure.x;
    out_color = vec4(clamp(neutral(color), 0.0, 1.0), 1.0);
}
