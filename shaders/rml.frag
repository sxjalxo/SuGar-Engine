#version 450

// Runtime UI (RmlUi) fragment shader — Phase 16B.2.
// Vertex colours arrive premultiplied; untextured geometry samples a 1x1 white
// texture, so this single path covers both cases. Blending is configured as
// (ONE, ONE_MINUS_SRC_ALPHA) to match premultiplied alpha.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec2 fragMaskCoord;

layout(set = 0, binding = 0) uniform sampler2D uTexture;
// RmlUi's clip mask, as a coverage texture rather than a stencil buffer: the UI pass
// writes into an image the engine already owns (D32_SFLOAT, no stencil aspect) and
// offscreen effect layers have no depth at all, so a stencil mask would have meant
// changing the engine's depth format for the scene pass too. A 1x1 white mask is
// bound when nothing is masked, which makes this multiply a no-op.
layout(set = 1, binding = 0) uniform sampler2D uClipMask;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(uTexture, fragTexCoord) * texture(uClipMask, fragMaskCoord).r;
}
