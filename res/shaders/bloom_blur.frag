#version 430 core

in vec2 vUV;
layout(location=0) out vec4 fragColor;

uniform sampler2D uImage;
uniform vec2 uDirection;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec2 offset = uDirection * texel;

    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    vec3 color = texture(uImage, vUV).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 sampleOffset = offset * float(i);
        color += texture(uImage, vUV + sampleOffset).rgb * weights[i];
        color += texture(uImage, vUV - sampleOffset).rgb * weights[i];
    }

    fragColor = vec4(color, 1.0);
}
