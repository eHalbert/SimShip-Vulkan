#version 450

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoords;

layout (binding = 0) uniform UBO {
    mat4  matViewProj;
    vec3  eyePos;
    float padding1;
    mat4  lightViewProj;
} ubo;
layout (binding = 1) uniform sampler2D displacement;

layout (location = 0) out vec3 vdir;
layout (location = 1) out vec2 tex;
layout (location = 2) out vec3 vertex;
layout (location = 3) out vec4 fragPosLightSpace;


void main() 
{
    vec4 posLocal = vec4(aPosition, 1.0);
    vec3 disp = texture(displacement, aTexCoords).xyz;
    vec3 posWorld = posLocal.xyz + disp;

    vdir = ubo.eyePos - posWorld;
    tex = aTexCoords;
    vertex = posWorld;
    fragPosLightSpace = ubo.lightViewProj * vec4(aPosition, 1.0);

    gl_Position = ubo.matViewProj * vec4(posWorld, 1.0);
}

