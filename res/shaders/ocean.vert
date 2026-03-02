#version 430 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

layout(location=0) uniform mat4 uView;
layout(location=1) uniform mat4 uProj;
layout(location=2) uniform float uTime;
layout(location=3) uniform vec3 uCameraPos; // optional for frag

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec3 worldPos = aPos; // later: apply Gerstner
    vWorldPos = worldPos;
    vNormal = aNormal;
    gl_Position = uProj * uView * vec4(worldPos, 1.0);
}