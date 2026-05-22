#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(binding = 0) uniform UBOShadow {
    mat4 lightSpaceMatrix;
    mat4 model;
} ubo;

layout(location = 0) out vec2 outTexCoord;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    outTexCoord = inTexCoord;
    gl_Position = ubo.lightSpaceMatrix * ubo.model * vec4(inPosition, 1.0);
}