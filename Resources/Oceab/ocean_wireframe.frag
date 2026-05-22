#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 FragColor;


void main() 
{
    vec3 color = pow(inColor.rgb, vec3(1.7));
    FragColor = vec4(color, 1.0);
}
