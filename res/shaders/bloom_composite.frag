#version 430 core

in vec2 vUV;
layout(location=0) out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;

void main()
{
    vec3 scene = texture(uScene, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;

    vec3 color = scene + bloom * 0.55;
    fragColor = vec4(color, 1.0);
}
