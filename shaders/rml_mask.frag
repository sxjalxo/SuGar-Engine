#version 450

// Writes RmlUi's clip mask (DevDocs/DESIGN_UI_CLIP_MASK.md).
//
// Two modes, because the four ClipMaskOperations need two different sources:
//   value  — write a constant (Set writes 1 into a 0-cleared mask, SetInverse writes 0
//            into a 1-cleared one, Union writes 1 with MAX blending).
//   sample — write what the PREVIOUS mask held here (Intersect: the target is cleared to
//            0 and only the new geometry's area keeps the old coverage, which is exactly
//            "old AND new"). Reading the mask being written is a feedback loop, so the
//            two masks ping-pong.

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec2 fragMaskCoord;

layout(set = 1, binding = 0) uniform sampler2D uPreviousMask;

layout(push_constant) uniform Push {
    vec2 viewport;
    vec2 translation;
    float value;      // coverage to write in `value` mode
    float sampleMode; // 0 = write `value`, 1 = write the previous mask's coverage
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    const float coverage = pc.sampleMode > 0.5
        ? texture(uPreviousMask, fragMaskCoord).r
        : pc.value;
    outColor = vec4(coverage, 0.0, 0.0, 1.0);
}
