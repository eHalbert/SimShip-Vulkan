#version 450

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aTexCoords;
layout (location = 2) in vec4 iModelMatrix0;  // mat4 modelMatrix[0]
layout (location = 3) in vec4 iModelMatrix1;  // mat4 modelMatrix[1]  
layout (location = 4) in vec4 iModelMatrix2;  // mat4 modelMatrix[2]
layout (location = 5) in vec4 iModelMatrix3;  // mat4 modelMatrix[3]
layout (location = 6) in int iLOD;

layout (binding = 0, std140) uniform UBO {
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
    int     bShowShadow;
    int     bShowReflection;
    int     bShowWake;
    vec2    shipSize;
    vec2	shipPivot;
    vec2    wakeSize;
	int     texLayer;
    float   pad;
} ubo;
layout (binding = 1) uniform sampler2D displacement;
layout (binding = 2) uniform sampler2DArray kelvinArray;

layout (location = 0) out vec3 vdir;
layout (location = 1) out vec2 tex;
layout (location = 2) out vec3 vertex;
layout (location = 3) out float lodLevel;
layout (location = 4) out float kelvinFoam;
layout (location = 5) out vec4 fragPosLightSpace;

const float PI_2 = 1.57079632;
const int kelvin_width_2 = 512; // independent of texture size
const int kelvin_height = 1024; // independent of texture size


void main() 
{
    // Reconstruction mat4 modelMatrix depuis 4 vec4
    mat4 modelMatrix = mat4(iModelMatrix0, iModelMatrix1, iModelMatrix2, iModelMatrix3);
    
	// Transform to world space
    vec4 posLocal = modelMatrix * vec4(aPosition, 1.0);
    vec3 disp = texture(displacement, aTexCoords).xyz;
    vec3 posWorld = posLocal.xyz + disp;
    kelvinFoam = 0.0;   // Out for the fragment shader

    if (ubo.bKelvinWakes > 0)
    {
        // Relative position of the vertex to the position of the ship (in world space)
        vec2 relativePos = posWorld.xz - ubo.shipPosition.xz;

        // Apply inverse boat rotation to bring coordinates into the ship's local frame
        float cosR = cos(-ubo.shipRotation - PI_2);
        float sinR = sin(-ubo.shipRotation - PI_2);
        
        // Rotate the relative position vector
        vec2 rotatedPos;
        rotatedPos.x = relativePos.x * cosR - relativePos.y * sinR;
        rotatedPos.y = relativePos.x * sinR + relativePos.y * cosR;

        // Compute texture coordinates for the Kelvin wake texture
        float texX = 0.5 + rotatedPos.x * ubo.kelvinScale / kelvin_width_2;
        float texY = (rotatedPos.y * ubo.kelvinScale / kelvin_height) + (ubo.centerFore / kelvin_height);
        
        // Only apply the wake effect if within the bounds of the Kelvin texture
        if (texX >= 0.0 && texX <= 1.0 && texY >= 0.0 && texY <= 1.0)
        {
            // Sample the grayscale value from the Kelvin wake texture array
            float kelvinGray = texture(kelvinArray, vec3(texX, texY, ubo.texLayer)).r;

            // Convert grayscale value [0,1] to vertical offset: 0.5 → 0, 0 → -amplitude, 1 → +amplitude
            float kelvinYoffset = (kelvinGray - 0.5) * 2.0 * ubo.amplitude;

            // Optional: fade the effect near the edges for a soft transition
            float border = 0.04;
            float mask = smoothstep(0.0, border, texX) * smoothstep(0.0, border, 1.0-texX) * smoothstep(0.0, border, texY) * smoothstep(0.0, border, 1.0-texY);
            kelvinYoffset *= mask;

            // Apply the vertical offset to the vertex position and foam intensity
            posWorld.y += kelvinYoffset;
            kelvinFoam = kelvinYoffset * (1.0 - texY);  // Less foam further from the ship
        }
    }
    
    // Outputs
    vertex = posWorld;
    vdir = ubo.eyePos - vertex;
    tex = aTexCoords;
    lodLevel = float(iLOD);
    fragPosLightSpace = ubo.lightSpaceMatrix * vec4(vertex, 1.0);

    gl_Position = ubo.matViewProj * vec4(vertex, 1.0);

}
