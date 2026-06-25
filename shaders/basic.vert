#version 450

const int MAX_LIGHTS = 4;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

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

layout(push_constant) uniform PushConstants
{
    mat4 model;
    float metallic;
    float roughness;
    float ao;
} pushConstants;

void main()
{
    vec4 worldPosition = pushConstants.model * vec4(inPos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPosition;

    fragPos = worldPosition.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(pushConstants.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragUV = inUV;
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPosition;
}
