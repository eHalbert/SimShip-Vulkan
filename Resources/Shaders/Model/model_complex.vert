#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform sMatrix {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;
} matrix;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec4 fragPosLightSpace;


void main()
{
    gl_Position = matrix.proj * matrix.view * matrix.model * vec4(inPosition, 1.0);
    fragPos = (matrix.model * vec4(inPosition, 1.0)).xyz;
    normal = mat3(matrix.model) * inNormal;
    texCoord = inTexCoord;
    fragPosLightSpace = matrix.lightViewProj * matrix.model * vec4(inPosition, 1.0);
}
