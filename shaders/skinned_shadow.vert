#version 450

// Phase 17C.2 — the shadow pass needs its own skinned variant, or an animated
// character casts its *bind pose* shadow: a limb visibly waves while its shadow
// stands still. The skinning must match skinned.vert exactly, since the two are
// rasterizing the same deformed vertices from different viewpoints.

const int MAX_JOINTS = 64;
const int MAX_LIGHTS = 4;

layout(location = 0) in vec3 inPos;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

// Mirrors basic.vert's block: the shadow pass binds the same descriptor set 0, so
// binding 0 is the whole scene UBO. Declaring a shorter struct here would read
// `view` as if it were the light-space matrix. See shadow.vert.
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

layout(set = 1, binding = 0) uniform JointBuffer
{
    mat4 joints[MAX_JOINTS];
} jointBuffer;

layout(push_constant) uniform PushConstants
{
    mat4 model;
} pushConstants;

void main()
{
    float total = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    mat4 skinMatrix = mat4(1.0);
    if (total > 0.0)
    {
        skinMatrix =
            inWeights.x * jointBuffer.joints[inJoints.x] +
            inWeights.y * jointBuffer.joints[inJoints.y] +
            inWeights.z * jointBuffer.joints[inJoints.z] +
            inWeights.w * jointBuffer.joints[inJoints.w];
        skinMatrix /= total;
    }

    gl_Position = ubo.lightSpaceMatrix * pushConstants.model * skinMatrix * vec4(inPos, 1.0);
}
