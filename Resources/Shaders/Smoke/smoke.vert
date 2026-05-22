#version 450

struct Particle {
    vec3    position;
    float   life;
    vec3    velocity;
    float   pad;
    vec4    color;
};

layout(set = 0, binding = 0, std430) readonly buffer Particles { 
    Particle particles[]; 
};

layout(location = 0) out vec4 ParticleColor;
layout(location = 1) out float ParticleLife;

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
    uint idx = gl_InstanceIndex;
    Particle p = particles[idx];
    
    if (p.life <= 0.0) {
        gl_Position = vec4(-1000.0, -1000.0, -1000.0, 1.0);
        ParticleColor = vec4(0.0);
        ParticleLife = 0.0;
        gl_PointSize = 0.0;
        return;
    }
    
    gl_Position = pushConstants.projection * pushConstants.view * vec4(p.position, 1.0);

    float age = 1.0 - (p.life / pushConstants.lifeSpan);
    gl_PointSize = 250.0 * p.life / gl_Position.w * (1.0 + 12.0 * age);

    float alpha = pushConstants.density * p.color.a * (p.life / pushConstants.lifeSpan) * 0.2;
    ParticleColor = vec4(p.color.rgb, alpha);
    ParticleLife = p.life;
}
