#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float t;

layout(push_constant) uniform PushConstants {
    mat4  model;
    mat4  view;
    mat4  proj;
    vec3  color;
    float intensity;
} pc;

layout(location = 0) out float v_t;


void main()
{
    gl_Position = pc.proj * pc.view * pc.model * vec4(position, 1.0);
    v_t = t;
}
