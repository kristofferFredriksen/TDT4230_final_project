#version 430 core

in vec3 vDir;
layout(location=0) out vec4 fragColor;

uniform samplerCube uSkybox;

void main()
{
    fragColor = texture(uSkybox, normalize(vDir));
}