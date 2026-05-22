#version 450

layout(location = 0) in float v_t;

layout(push_constant) uniform PushConstants {
    mat4  model;
    mat4  view;
    mat4  proj;
    vec3  color;
    float intensity;
} pc;

layout(location = 0) out vec4 FragColor;


void main()
{
    float localIntensity = pc.intensity * v_t; // intense near the lighthouse, none at the far end
    if (localIntensity < 0.05)
        discard;
    FragColor = vec4(pc.color, localIntensity);
}
