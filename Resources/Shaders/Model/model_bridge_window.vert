#version 450

// Vertex input
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// Sortie vers fragment
layout(location = 0) out vec2 outTexCoord;

void main()
{
    outTexCoord = inTexCoord;
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
}