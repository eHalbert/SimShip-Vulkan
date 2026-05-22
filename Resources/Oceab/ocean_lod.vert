#version 450

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoords;
layout (location = 2) in vec4 iModelMatrix0;  // mat4 modelMatrix[0]
layout (location = 3) in vec4 iModelMatrix1;  // mat4 modelMatrix[1]  
layout (location = 4) in vec4 iModelMatrix2;  // mat4 modelMatrix[2]
layout (location = 5) in vec4 iModelMatrix3;  // mat4 modelMatrix[3]
layout (location = 6) in int iLOD;

layout(std140, binding = 0) uniform OceanUBO {
    mat4    matViewProj;
    vec3    eyePos;
    int     bEnvmap;
    mat4    lightSpaceMatrix;
    vec3    oceanColor;
    float   transparency;
    vec3    sunColor;
    float   time;
    vec3    sunDir;
    float   exposure;
    vec3    shipPosition;
    float   shipRotation;
    int     bKelvinWakes;
    float   amplitude;
    float   kelvinScale;
    float   centerFore;
    int     bShowPatches;
} ubo;
layout (binding = 1) uniform sampler2D displacement;

layout (location = 0) out vec3 vdir;
layout (location = 1) out vec2 tex;
layout (location = 2) out vec3 vertex;
layout (location = 3) out float lodLevel;


void main() 
{
    // Reconstruction mat4 modelMatrix depuis 4 vec4
    mat4 modelMatrix = mat4( iModelMatrix0, iModelMatrix1, iModelMatrix2, iModelMatrix3 );
    
	// Transform to world space
    vec4 posWorld = modelMatrix * vec4(aPosition, 1.0);
    vec3 disp = texture(displacement, aTexCoords).xyz;
    vec3 posFinal = posWorld.xyz + disp;

    // Outputs
    vdir = ubo.eyePos - posFinal;
    tex = aTexCoords;
    vertex = posFinal;
    lodLevel = float(iLOD);

    gl_Position = ubo.matViewProj * vec4(posFinal, 1.0);
}
