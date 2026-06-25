#version 450

layout(location = 0) in vec3 inPos;

layout(binding = 0) uniform ShadowUniformBufferObject
{
    mat4 lightSpaceMatrix;
} ubo;

layout(push_constant) uniform PushConstants
{
    mat4 model;
} pushConstants;

void main()
{
    gl_Position = ubo.lightSpaceMatrix * pushConstants.model * vec4(inPos, 1.0);
}
