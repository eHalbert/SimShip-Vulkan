#version 450

layout(location = 0) in vec2 inTexCoord;

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;  // même binding que ton MsPipeline

void main()
{
    if (texture(albedoTexture, inTexCoord).a < 0.2)
        discard;
}