#version 450

layout(location = 0) in vec3  aPos;
layout(location = 1) in float aAlpha;

layout(binding = 0, std140) uniform WakeUBO {
    float scaleX;
    float scaleZ;
    float offsetX;
    float offsetZ;
    float originX;
    float originZ;
} ubo;

layout(location = 0) out float FragAlpha;

void main()
{
    float x = (aPos.x - ubo.originX) * ubo.scaleX + ubo.offsetX;
    float y = (aPos.z - ubo.originZ) * ubo.scaleZ + ubo.offsetZ;
    gl_Position = vec4(x, y, 0.0, 1.0);
    FragAlpha = aAlpha;
}