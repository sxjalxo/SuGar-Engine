#version 450

const int MAX_LIGHTS = 4;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D shadowMap;

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

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 0) out vec4 outColor;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) {
        return 0.0;
    }

    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float bias = max(0.0025 * (1.0 - dot(normal, lightDir)), 0.0005);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closestDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += projCoords.z - bias > closestDepth ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

void main() {
    vec3 albedo = texture(texSampler, fragUV).rgb;
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);
    float metallic = clamp(pushConstants.metallic, 0.0, 1.0);
    float roughness = clamp(pushConstants.roughness, 0.04, 1.0);
    float ao = clamp(pushConstants.ao, 0.0, 1.0);
    vec3 ambient = 0.12 * ao * albedo;
    vec3 lighting = vec3(0.0);
    vec3 baseReflectivity = mix(vec3(0.04), albedo, metallic);

    int activeLightCount = min(ubo.lightCount, MAX_LIGHTS);
    for (int i = 0; i < activeLightCount; i++) {
        vec3 lightDir = normalize(ubo.lightPositions[i].xyz - fragPos);
        vec3 halfVector = normalize(viewDir + lightDir);
        float diffuseStrength = max(dot(normal, lightDir), 0.0);
        float halfwayStrength = max(dot(normal, halfVector), 0.0);
        float specPower = mix(2.0, 256.0, 1.0 - roughness);
        float specularStrength = pow(halfwayStrength, specPower);
        float shadow = i == 0 ? ShadowCalculation(fragPosLightSpace, normal, lightDir) : 0.0;

        vec3 diffuse = (1.0 - metallic) * albedo * diffuseStrength;
        vec3 specular = baseReflectivity * specularStrength * diffuseStrength;
        lighting += (1.0 - shadow) * (diffuse + specular) * ubo.lightColors[i].rgb;
    }

    vec3 color = ambient + lighting;
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
