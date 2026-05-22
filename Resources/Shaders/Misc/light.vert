#version 450

layout(location = 0) in vec2 position;

layout(push_constant) uniform PushConstants {
    mat4  model;
    mat4  view;
    mat4  proj;
    vec3  lightColor;
    float lightIntensity;
    float starIntensity;
    float pad[3];
} pc;

layout(location = 0) out vec2 fragUV;

void main()
{
    fragUV = position * 0.5 + 0.5;

    gl_Position = pc.proj * pc.view * pc.model * vec4(position, 0.0, 1.0);
}
