#version 430 core

layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;

layout(location=0) uniform vec2 uScreenSize;

out vec4 vColor;

void main()
{
    vec2 ndc = vec2(
        (aPos.x / uScreenSize.x) * 2.0 - 1.0,
        1.0 - (aPos.y / uScreenSize.y) * 2.0
    );
    vColor = aColor;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
