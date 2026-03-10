#version 430 core

in vec2 vUV;
layout(location=0) out vec4 fragColor;

uniform sampler2D uScene;

void main()
{
    vec3 color = texture(uScene, vUV).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float threshold = 0.82;
    float softKnee = 0.35;
    float kneeStart = threshold - softKnee;
    float weight = clamp((luminance - kneeStart) / max(softKnee, 1e-4), 0.0, 1.0);
    weight *= smoothstep(threshold, threshold + 0.35, luminance);
    fragColor = vec4(color * weight, 1.0);
}
