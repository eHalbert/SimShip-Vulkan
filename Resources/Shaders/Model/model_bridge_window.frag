#version 450

// Entrée depuis vertex
layout(location = 0) in vec2 inTexCoord;

// Set 1 : texture du mesh (même binding que tous tes autres pipelines)
layout(set = 1, binding = 0) uniform sampler2D texDiffuse;

void main()
{
    vec4 color = texture(texDiffuse, inTexCoord);

    // Discard les pixels transparents (ne pas écrire stencil=2 là où il n'y a pas de vitre)
    if (color.a < 0.2)
        discard;

    // Pas d'écriture color (attachmentCount = 0 dans le pipeline)
    // Seul le stencil est écrit par le pipeline
}