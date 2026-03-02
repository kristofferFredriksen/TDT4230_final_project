#version 430 core

in vec3 vNormal;
in vec3 vWorldPos;

layout(location=3) uniform vec3 uCameraPos;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.3, 1.0, 0.2));
    float ndotl = max(dot(N, L), 0.0);
    vec3 col = vec3(0.02, 0.15, 0.25) * (0.2 + 0.8 * ndotl);
    fragColor = vec4(col, 1.0);
}