#version 450

layout (location = 0) in vec2 TexCoords;
  
layout (binding = 0) uniform sampler2D sky;
layout (binding = 1) uniform sampler2D clouds;
layout (binding = 2, std140) uniform CloudsPostUBO {
	vec2	cloudsResolution;
	int		bShowSky;
	int		bShowClouds;
};

layout (location = 0) out vec4 FragColor;

#define  OFFSET_X  (1.0 / cloudsResolution.x)
#define  OFFSET_Y  (1.0 / cloudsResolution.y)

vec2 offsets[25] = vec2[](
    vec2(-2.0 * OFFSET_X,  2.0 * OFFSET_Y),
    vec2(-1.0 * OFFSET_X,  2.0 * OFFSET_Y),
    vec2(       0.0,        2.0 * OFFSET_Y),
    vec2( 1.0 * OFFSET_X,  2.0 * OFFSET_Y),
    vec2( 2.0 * OFFSET_X,  2.0 * OFFSET_Y),

    vec2(-2.0 * OFFSET_X,  1.0 * OFFSET_Y),
    vec2(-1.0 * OFFSET_X,  1.0 * OFFSET_Y),
    vec2(       0.0,        1.0 * OFFSET_Y),
    vec2( 1.0 * OFFSET_X,  1.0 * OFFSET_Y),
    vec2( 2.0 * OFFSET_X,  1.0 * OFFSET_Y),

    vec2(-2.0 * OFFSET_X,  0.0),
    vec2(-1.0 * OFFSET_X,  0.0),
    vec2(       0.0,        0.0),
    vec2( 1.0 * OFFSET_X,  0.0),
    vec2( 2.0 * OFFSET_X,  0.0),

    vec2(-2.0 * OFFSET_X, -1.0 * OFFSET_Y),
    vec2(-1.0 * OFFSET_X, -1.0 * OFFSET_Y),
    vec2(       0.0,       -1.0 * OFFSET_Y),
    vec2( 1.0 * OFFSET_X, -1.0 * OFFSET_Y),
    vec2( 2.0 * OFFSET_X, -1.0 * OFFSET_Y),

    vec2(-2.0 * OFFSET_X, -2.0 * OFFSET_Y),
    vec2(-1.0 * OFFSET_X, -2.0 * OFFSET_Y),
    vec2(       0.0,       -2.0 * OFFSET_Y),
    vec2( 1.0 * OFFSET_X, -2.0 * OFFSET_Y),
    vec2( 2.0 * OFFSET_X, -2.0 * OFFSET_Y)
);
const float kernel[25] = float[](
     1.0 / 273.0,  4.0 / 273.0,  7.0 / 273.0,  4.0 / 273.0,  1.0 / 273.0,
     4.0 / 273.0, 16.0 / 273.0, 26.0 / 273.0, 16.0 / 273.0,  4.0 / 273.0,
     7.0 / 273.0, 26.0 / 273.0, 41.0 / 273.0, 26.0 / 273.0,  7.0 / 273.0,
     4.0 / 273.0, 16.0 / 273.0, 26.0 / 273.0, 16.0 / 273.0,  4.0 / 273.0,
     1.0 / 273.0,  4.0 / 273.0,  7.0 / 273.0,  4.0 / 273.0,  1.0 / 273.0
);
vec4 gaussianBlur(vec2 uv)
{
    vec4 col = vec4(0.0);
    for (int i = 0; i < 25; i++)
        col += texture(clouds, uv + offsets[i]) * kernel[i];

    return col;
}


void main()
{
    FragColor.rgb = vec3(0.0);

    if (bShowSky > 0)       FragColor += texture(sky, TexCoords);
    if (bShowClouds > 0)    FragColor += gaussianBlur(TexCoords);
	FragColor.a = 1.0;
}