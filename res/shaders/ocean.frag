#version 430 core

in vec3 vNormal;
in vec3 vWorldPos;

layout(location=2) uniform float uTime;
layout(location=3) uniform vec3 uCameraPos;
layout(location=400) uniform int uDebugMode;

out vec4 fragColor;

const float PI = 3.14159265358979323846;

uniform samplerCube uSkybox;

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    // Schlick approximation
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec2 xz = vWorldPos.xz;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Directional sun light (pointing *from* surface *towards* the sun)
    vec3 L = normalize(vec3(0.3, 1.0, 0.2));
    vec3 H = normalize(L + V);

    vec3 microN = vec3(0.0, 1.0, 0.0);
    float microAmp = 0.08; // strength

    for (int i = 0; i < 3; i++) {
        float a = float(i) * 1.7;
        vec2 dir = normalize(vec2(cos(a), sin(a)));
        float freq = 2.5 + 1.5 * float(i);
        float phase = dot(dir, xz) * freq + uTime * (1.5 + 0.7 * float(i));
        microN.x += microAmp * dir.x * cos(phase);
        microN.z += microAmp * dir.y * cos(phase);
    }

    microN = normalize(microN);

    // Blend analytic normal with micro detail
    N = normalize(mix(N, microN, 0.25));

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Debug toggles you can later control with a uniform
    if (uDebugMode == 1) {
        fragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    // --- Base water colors (tune) ---
    vec3 deepColor    = vec3(0.02, 0.10, 0.18);
    vec3 shallowColor = vec3(0.05, 0.22, 0.28);

    float distanceToCamera = length(uCameraPos - vWorldPos);
    float density = 0.015;
    float depthFactor = clamp(1.0 - exp(-distanceToCamera * density), 0.0, 1.0);

    if (uDebugMode == 2) {
        fragColor = vec4(vec3(depthFactor), 1.0);
        return;
    }

    // Depth-like factor (cheap approximation):
    vec3 waterColor = mix(shallowColor, deepColor, depthFactor);
    // Diffuse term (kept small for water; most of the "look" comes from specular + Fresnel)
    vec3 diffuse = waterColor * (0.06 + 0.18 * NdotL);

    // Fresnel reflectance: water F0 is low (~0.02) but rises strongly at grazing angles
    vec3 F0 = vec3(0.04);
    vec3 F = fresnelSchlick(NdotV, F0);

    float slope = 1.0 - NdotV;              // grazing => more blur
    float roughness = mix(0.05, 0.25, slope);
    float maxLod = 6.0;              // depends on cubemap size (512 => ~9 mips, 6 is safe)

    // Reflection direction for environment lookup
    vec3 R = reflect(-V, N);
    vec3 env = textureLod(uSkybox, R, roughness * maxLod).rgb;

    if (uDebugMode == 3) {
        fragColor = vec4(env, 1.0);
        return;
    }

    vec3 reflection = env * (F * 1.3);

    // Specular highlight (Blinn-Phong for now)
    float NdotH = max(dot(N, H), 0.0);
    float shininess = 200.0; // higher = tighter glints
    float spec = pow(NdotH, shininess) * NdotL;
    vec3 specular = vec3(1.0) * spec;

    // Combine:
    // - Reflection dominates at grazing angles via Fresnel
    // - Diffuse provides body color
    // - Specular adds sun glints
    vec3 color = diffuse * (1.0 - F) + reflection + specular;
    fragColor = vec4(color, 1.0);
}