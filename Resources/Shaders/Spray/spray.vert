#version 450

layout(location = 0) in vec3  aPos;
layout(location = 1) in float aLife;
layout(location = 2) in vec4  aColor;

layout(binding = 0) uniform UBO
{
    mat4    view;
    mat4    proj;
    float   density;
    float   lifeSpan;
    float   exposure;
    float   padding;
} ubo;

layout(location = 0) out vec4 ParticleColor;


void main()
{
    gl_Position = ubo.proj * ubo.view * vec4(aPos, 1.0);
    gl_PointSize = 1000.0 / gl_Position.w;
    
    float alpha = 3.0 * ubo.density * aLife / ubo.lifeSpan;
    ParticleColor = vec4(aColor.rgb * ubo.exposure, alpha);
}
