#version 450

layout(location = 0) in vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4  model;
    mat4  view;
    mat4  proj;
    vec3  lightColor;
    float lightIntensity;
    float starIntensity;
    float pad[3];
} pc;

layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 centeredUV = fragUV - vec2(0.5);
    float dist = length(centeredUV);

    float halo = exp(-dist * dist * 40.0);

    float angle = atan(centeredUV.y, centeredUV.x);

    float star = pow(max(0.0, sin(8.0 * angle)), 6.0);

    float intensity = pc.lightIntensity * (halo + pc.starIntensity * star);
    if (intensity < 0.2)
        discard;

    FragColor = vec4(pc.lightColor * intensity, intensity);
}