#version 450

// Canonical affine planes keep independently clipped coplanar surfaces tied.
// Conservative upper bounds retain early depth rejection for hidden geometry.
layout(location = 0) in vec3 position;
layout(location = 1) in uvec4 material;
layout(location = 2) in uint flags;
layout(location = 3) in vec4 plane;
layout(location = 0) flat out uvec4 fragmentMaterial;
layout(location = 1) flat out vec3 fragmentPlane;
layout(push_constant) uniform View {
    float width;
    float height;
    float depthScale;
    uint reserved;
} view;
const uint BACKGROUND = 1u;
const uint DECAL = 2u;
const float MINIMUM_DEPTH = 0.0000001;
const float MAXIMUM_DEPTH = 0.9999;
const float DECAL_SCALE = 1.0001;
const float UPPER_RELATIVE_MARGIN = 0.00000762939453125;
void main() {
    vec3 canonical = plane.xyz * view.depthScale;
    float maximum = plane.w * view.depthScale;
    if ((flags & BACKGROUND) != 0u) {
        canonical = vec3(0.0, 0.0, MINIMUM_DEPTH);
        maximum = MINIMUM_DEPTH;
    }
    if ((flags & DECAL) != 0u) {
        canonical *= DECAL_SCALE;
        maximum *= DECAL_SCALE;
    }
    float depth = fma(canonical.x, position.x, fma(canonical.y, position.y, canonical.z));
    float magnitude = abs(canonical.x) * view.width + abs(canonical.y) * view.height
                      + abs(canonical.z);
    float margin = max(0.000001, magnitude * UPPER_RELATIVE_MARGIN);
    float upper = depth + margin;
    // Clamping one vertex at the upper limit could bend the bound below the
    // affine plane. Use the maximum for the entire triangle in that case.
    upper = maximum + margin >= MAXIMUM_DEPTH
            ? MAXIMUM_DEPTH : clamp(upper, MINIMUM_DEPTH, MAXIMUM_DEPTH);
    vec2 normalized = vec2(position.x / view.width, position.y / view.height) * 2.0 - 1.0;
    gl_Position = vec4(normalized, upper, 1.0);
    fragmentMaterial = material;
    fragmentPlane = canonical;
}
