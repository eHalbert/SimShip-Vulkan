#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(binding = 0) uniform sampler2D displacement;
layout(binding = 1) uniform UBO {
    mat4 model;
    mat4 view; 
    mat4 proj;
    vec4 color;
} ubo;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

void main() 
{
    outTexCoord = inTexCoord;
    outColor = ubo.color;
    vec3 disp = texture(displacement, inTexCoord).xyz;
    vec3 finalPos = inPosition + disp; 
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(finalPos, 1.0);
}
