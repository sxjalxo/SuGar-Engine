#version 450

// Instanced depth-only pass. The scene pass batching would be half a win on its own:
// every instanced object still has to be rasterized into the shadow map, so the same
// batches are replayed here with the model matrix read per instance.

const int MAX_LIGHTS = 8;

layout(location = 0) in vec3 inPos;

layout(location = 5) in vec4 inModel0;
layout(location = 6) in vec4 inModel1;
layout(location = 7) in vec4 inModel2;
layout(location = 8) in vec4 inModel3;
layout(location = 9) in vec4 inBaseColor; // unused here; the instance stride is shared

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
    gl_Position = ubo.lightSpaceMatrix * model * vec4(inPos, 1.0);
}
