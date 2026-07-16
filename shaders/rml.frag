#version 450

// Runtime UI (RmlUi) fragment shader — Phase 16B.2.
// Vertex colours arrive premultiplied; untextured geometry samples a 1x1 white
// texture, so this single path covers both cases. Blending is configured as
// (ONE, ONE_MINUS_SRC_ALPHA) to match premultiplied alpha.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(uTexture, fragTexCoord);
}
