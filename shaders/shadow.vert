#version 450

const int MAX_LIGHTS = 4;

layout(location = 0) in vec3 inPos;

// The shadow pass binds the *same* descriptor set 0 as the scene pass, so binding 0
// is the whole scene UniformBufferObject — not a shadow-specific struct. This block
// must therefore mirror basic.vert's exactly: declaring only `mat4 lightSpaceMatrix`
// here silently reads offset 0, which is `view`, and renders the shadow map from the
// camera instead of the light. The fragment stage samples the map with the real
// lightSpaceMatrix, so the two disagree and the shadows are wrong.
layout(binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 viewPos;
    vec4 lightPositions[MAX_LIGHTS];
    vec4 lightColors[MAX_LIGHTS];
    int lightCount;
} ubo;

layout(push_constant) uniform PushConstants
{
    mat4 model;
} pushConstants;

void main()
{
    gl_Position = ubo.lightSpaceMatrix * pushConstants.model * vec4(inPos, 1.0);
}
