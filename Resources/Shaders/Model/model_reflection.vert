#version 450

layout(set = 0, binding = 0) uniform sMatrix {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 clipPlane;
} matrix;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// OUTPUTS
layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outTexCoord;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[1];  // 1 seul plan (GL_CLIP_DISTANCE0)
};
void main()
{
    vec4 worldPos = matrix.model * vec4(inPosition, 1.0);
    
    // Calcul distance signée au plan Y=0 (nx*x + ny*y + nz*z + d)
    gl_ClipDistance[0] = dot(worldPos, matrix.clipPlane);
    
    gl_Position = matrix.proj * matrix.view * worldPos;
    fragPos = worldPos.xyz;
    outNormal = mat3(matrix.model) * inNormal;
    outTexCoord = inTexCoord;
}
