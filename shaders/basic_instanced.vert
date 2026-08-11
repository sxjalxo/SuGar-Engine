#version 450

// Instanced scene vertex shader (M4 L3). Identical to basic.vert except that the
// per-object data — model matrix and base colour — arrives as *per-instance vertex
// attributes* (binding 1) instead of push constants, so one draw call can cover many
// objects that share a mesh and a material. A pooled particle system is the forcing case:
// 4 000 pooled particles were 4 000 draw calls and cost 58% of the frame rate.

const int MAX_LIGHTS = 8;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// Per-instance (binding 1). A mat4 occupies four consecutive attribute locations.
layout(location = 5) in vec4 inModel0;
layout(location = 6) in vec4 inModel1;
layout(location = 7) in vec4 inModel2;
layout(location = 8) in vec4 inModel3;
layout(location = 9) in vec4 inBaseColor;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec4 fragBaseColor;

layout(binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 viewPos;
    vec4 lightPositions[MAX_LIGHTS];
    vec4 lightColors[MAX_LIGHTS];
    vec4 ambient;
    int lightCount;
} ubo;

void main()
{
    mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
    vec4 worldPosition = model * vec4(inPos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPosition;

    fragPos = worldPosition.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragUV = inUV;
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPosition;
    fragBaseColor = inBaseColor;
}
