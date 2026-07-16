#version 450

// Runtime UI (RmlUi) vertex shader — Phase 16B.2.
// RmlUi emits geometry in pixel space with a per-draw translation. Map it straight
// to Vulkan NDC: y=0 is the top of the screen and Vulkan's NDC y=-1 is also the top,
// so no flip is needed here.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;   // premultiplied alpha, R8G8B8A8_UNORM
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform Push {
    vec2 viewport;    // framebuffer size in pixels
    vec2 translation; // per-draw offset from RenderGeometry
} pc;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    vec2 pixel = inPosition + pc.translation;
    vec2 ndc = (pixel / pc.viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
