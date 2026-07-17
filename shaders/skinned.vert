#version 450

// Phase 17C.2 — GPU skinning. This shader is the *only* thing in the renderer that
// knows a mesh is skinned, and it knows nothing about animation: it consumes joint
// matrices someone else derived (Skinning::computeJointMatrices) and blends
// vertices with them. Skinning = f(mesh, skeleton pose).
//
// Identical to basic.vert apart from the skin matrix, deliberately: the fragment
// stage, lighting, shadows and push constants are shared, so a skinned mesh is lit
// and shadowed by exactly the same code as a static one.

const int MAX_LIGHTS = 4;

// Must match MAX_JOINTS in BasicTrianglePass.cpp. 64 mat4 = 4 KiB, comfortably
// inside the 16 KiB every Vulkan implementation guarantees for a UBO range.
const int MAX_JOINTS = 64;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragPosLightSpace;

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

// Set 1: the current pose, one matrix per joint, bound with a dynamic offset so
// every skinned draw in a frame reads its own slice of one buffer.
layout(set = 1, binding = 0) uniform JointBuffer
{
    mat4 joints[MAX_JOINTS];
} jointBuffer;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    float metallic;
    float roughness;
    float ao;
} pushConstants;

void main()
{
    // Linear blend skinning. Weights sum to 1 in a well-formed asset; a vertex whose
    // weights are all zero would collapse to the origin, so it falls back to
    // identity and simply renders unskinned.
    float total = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    mat4 skinMatrix = mat4(1.0);
    if (total > 0.0)
    {
        skinMatrix =
            inWeights.x * jointBuffer.joints[inJoints.x] +
            inWeights.y * jointBuffer.joints[inJoints.y] +
            inWeights.z * jointBuffer.joints[inJoints.z] +
            inWeights.w * jointBuffer.joints[inJoints.w];
        skinMatrix /= total; // tolerate assets whose weights don't quite normalize
    }

    // The joint matrices already cancelled the skinned node's own world transform,
    // so `model` applies here exactly as it does for static geometry — which is why
    // the draw path needs no skinned special case.
    vec4 worldPosition = pushConstants.model * skinMatrix * vec4(inPos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPosition;

    fragPos = worldPosition.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(pushConstants.model * skinMatrix)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragUV = inUV;
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPosition;
}
