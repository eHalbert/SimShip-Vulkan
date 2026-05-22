#version 450

layout(location = 0) in vec4 ParticleColor;

layout(location = 0) out vec4 FragColor;


void main()
{
    vec2 circCoord = 2.0 * gl_PointCoord - 1.0;
    float dist = length(circCoord);

    float alpha = smoothstep(1.0, 0.0, dist);
    alpha = pow(alpha, 2.0);

    FragColor = vec4(ParticleColor.rgb, ParticleColor.a * alpha);
}
