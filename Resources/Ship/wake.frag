#version 450

layout(location = 0) in float FragAlpha;

layout(location = 0) out float FragColor;

void main()
{
    FragColor = FragAlpha;
}