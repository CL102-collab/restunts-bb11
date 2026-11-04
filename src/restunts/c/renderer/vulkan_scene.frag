#version 450

layout(location = 0) flat in uvec4 material;
layout(location = 0) out vec4 fragmentColor;
layout(location = 1) flat in vec3 fragmentPlane;
layout(depth_less) out float gl_FragDepth;
layout(std430, binding = 0) readonly buffer Palette { uint argb[]; } palette;

const uint PAINT_PATTERN = 1u;
const uint PAINT_ALTERNATE = 2u;
const uint PAINT_SHADOW = 3u;
const uint PALETTE_MASK = 255u;
const float CHANNEL_SCALE = 1.0 / 255.0;

void main() {
    gl_FragDepth = clamp(fma(fragmentPlane.x, gl_FragCoord.x,
                            fma(fragmentPlane.y, gl_FragCoord.y, fragmentPlane.z)),
                        0.0000001, 0.9999);
    uint color = material.x;
    uint mode = material.w;
    if (mode == PAINT_SHADOW) {
        fragmentColor = vec4(0.0, 0.0, 0.0, float(material.y & PALETTE_MASK) * CHANNEL_SCALE);
        return;
    }
    if (mode != 0u) {
        uvec2 pixel = uvec2(gl_FragCoord.xy);
        uint bit = ((pixel.y & 1u) == 0u ? 8u : 0u) + 7u - (pixel.x & 7u);
        bool set = (material.z & (1u << bit)) != 0u;
        if (mode == PAINT_PATTERN && !set) { discard; }
        if (mode == PAINT_ALTERNATE && set) { color = material.y; }
    }
    uint argb = palette.argb[color & PALETTE_MASK];
    fragmentColor = vec4(float((argb >> 16u) & PALETTE_MASK),
                         float((argb >> 8u) & PALETTE_MASK),
                         float(argb & PALETTE_MASK),
                         float((argb >> 24u) & PALETTE_MASK)) * CHANNEL_SCALE;
}
