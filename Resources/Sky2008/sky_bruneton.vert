#version 450

layout(location = 0) in vec2 inPosition;  
layout(location = 1) in vec2 inTexCoord; 

layout(set = 0, binding = 0) uniform sUBO {
	mat4    invProj;
	mat4    invView;
	vec3    camera;
	float   mieG;
	vec3    sunDir;
	float   sunIntensity;
} ubo;

layout(location = 0) out vec3 ViewDir;
layout(location = 1) out vec3 Camera;
layout(location = 2) out vec3 SunDir;
layout(location = 3) out float SunIntensity;
layout(location = 4) out float MieG;

const mat3 directxToVulkan = mat3(
    0.0, 1.0, 0.0,  // X_classic = Y_bruneton (nord)
    0.0, 0.0, 1.0,  // Y_classic = Z_bruneton (haut)
    1.0, 0.0, 0.0   // Z_classic = X_bruneton (est)
);

void main() 
{
    vec4 screenPos = vec4(inPosition, 0.9999999, 1.0);
    ViewDir = directxToVulkan * (ubo.invView * ubo.invProj * screenPos).xyz;
    Camera = ubo.camera;
    Camera.z = abs(Camera.z);
    SunDir = directxToVulkan * ubo.sunDir;
    SunIntensity = ubo.sunIntensity;
    MieG = ubo.mieG;

    gl_Position = screenPos;    
}