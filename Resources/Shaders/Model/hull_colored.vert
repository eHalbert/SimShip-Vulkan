#version 450

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout(binding = 0, std140) uniform UBO {
    mat4 model;
    mat4 view; 
    mat4 projection;
}ubo;

layout (location = 0) out vec3 fragmentColor;

void main()
{
    fragmentColor = aColor;
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPos, 1.0);
}