#version 450

layout(location = 0) in vec4 ParticleColor;
layout(location = 1) in float ParticleLife;

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 projection;
    float density;
    float lifeSpan;
    float exposure;
    float padding;
} pushConstants;


void main() 
{
    vec2 circCoord = 2.0 * gl_PointCoord - 1.0;
    float dist = length(circCoord);
    
    float alpha = smoothstep(1.0, 0.0, dist);
    alpha = pow(alpha, 0.8);
    
    FragColor = vec4(ParticleColor.rgb * pushConstants.exposure, ParticleColor.a * alpha * 4.0);

    if (FragColor.a < 0.01)
        discard;
}
