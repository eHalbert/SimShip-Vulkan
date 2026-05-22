#version 450
layout(location = 0) in vec3 aPos;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 color;
} ubo;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(aPos, 1.0);
    fragColor = ubo.color;
}
