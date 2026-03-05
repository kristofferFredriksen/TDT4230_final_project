#version 430 core

layout(location=0) in vec3 aPos;

layout(location=0) uniform mat4 uView;
layout(location=1) uniform mat4 uProj;

out vec3 vDir;

void main()
{
    // Remove translation from view matrix so the skybox stays centered on camera
    mat4 viewNoTranslation = mat4(mat3(uView));
    vec4 clip = uProj * viewNoTranslation * vec4(aPos, 1.0);

    // Push to far plane (so it never intersects)
    gl_Position = clip.xyww;

    // Use cube vertex direction as lookup direction
    vDir = aPos;
}