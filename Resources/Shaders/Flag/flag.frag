#version 450

layout(binding = 1) uniform sampler2D flagTexture;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
    float exposure;
    float pad[2];
} ubo;

layout(location = 0) in vec2 TexCoord;

layout(location = 0) out vec4 FragColor;


void main() {
    FragColor = texture(flagTexture, TexCoord);
    FragColor.rgb *= ubo.exposure;
}