#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
    float pad[2];
} ubo;

layout(location = 0) out vec2 TexCoord;


float rand(float x) {
    return fract(sin(x) * 43758.5453);
}


void main() {
    float noise = rand(aPos.x * 10.0 + ubo.time * 5.0) * 0.03;
    float wave  = sin(10.0 * aPos.x + ubo.time * 5.0) * 0.05;
    vec3 displacedPos = aPos + vec3(0.0, wave + noise, 0.0);

    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(displacedPos, 1.0);
    TexCoord = aTexCoord;
}