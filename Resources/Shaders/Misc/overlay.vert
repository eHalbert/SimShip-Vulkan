#version 450

layout(push_constant) uniform PC {
    vec2 topLeft;
    vec2 size;
    vec2 screenSize;
};
layout(location = 0) out vec2 outUV;

void main() {
    // Quad sans vertex buffer
    vec2 uv = vec2((gl_VertexIndex & 1), (gl_VertexIndex >> 1));
    outUV = uv;

    // Pixel → NDC
    vec2 pixelPos = topLeft + uv * size;
    vec2 ndc      = (pixelPos / screenSize) * 2.0 - 1.0;
    gl_Position   = vec4(ndc, 0.0, 1.0);
}