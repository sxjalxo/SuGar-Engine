#version 450

// Composites one RmlUi layer onto another. radius == 0 is a plain copy (CompositeLayers
// with no filter); radius > 0 applies a single-pass Gaussian blur (the box-shadow "blur"
// filter). Colours are premultiplied alpha throughout, so a weighted sum is correct, and
// the pipeline's premultiplied "over" blend does the actual layer composite.
layout(binding = 0) uniform sampler2D src;

layout(push_constant) uniform PushConstants {
    vec2 texelSize; // 1.0 / layer dimensions
    float sigma;
    int radius;     // 0 = copy; else Gaussian half-kernel size (clamped on the CPU)
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    if (pc.radius <= 0) {
        outColor = texture(src, uv);
        return;
    }
    vec4 sum = vec4(0.0);
    float weightSum = 0.0;
    float twoSigma2 = 2.0 * pc.sigma * pc.sigma;
    for (int y = -pc.radius; y <= pc.radius; ++y) {
        for (int x = -pc.radius; x <= pc.radius; ++x) {
            float w = exp(-float(x * x + y * y) / twoSigma2);
            sum += texture(src, uv + vec2(float(x), float(y)) * pc.texelSize) * w;
            weightSum += w;
        }
    }
    outColor = sum / weightSum;
}
