#version 430 core

in vec3 vNormal;
in vec3 vWorldPos;

layout(location=2) uniform float uTime;
layout(location=3) uniform vec3 uCameraPos;
layout(location=7) uniform float uWindAngleRad;
layout(location=400) uniform int uDebugMode;

out vec4 fragColor;

const float PI = 3.14159265358979323846;
const float G  = 9.81;

uniform samplerCube uSkybox;

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    // Schlick approximation
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-4);
}

float geometrySchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-4);
}

float geometrySmith(float NdotV, float NdotL, float roughness)
{
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

mat2 rotation2D(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

vec2 detailWaveSlope(vec2 xz, vec2 dir, float wavelength, float amplitude, float speed, float t)
{
    float k = 2.0 * PI / wavelength;
    float omega = sqrt(G * k);
    float phase = k * dot(dir, xz) - omega * speed * t;
    float dHdDir = amplitude * k * cos(phase);
    return dHdDir * dir;
}

vec2 compositeDetailSlope(vec2 xz, float t, float windAngle)
{
    mat2 windRot = rotation2D(windAngle);
    vec2 slope = vec2(0.0);

    // Wind-aligned short-wave bands for glints and choppiness.
    for (int i = 0; i < 6; ++i) {
        float fi = float(i);
        float spread = mix(-0.55, 0.55, fi / 5.0);
        vec2 dir = normalize(windRot * vec2(cos(spread), sin(spread)));

        float wavelength = mix(1.4, 10.0, pow(fi / 5.0, 1.35));
        float amplitude = mix(0.010, 0.045, 1.0 - fi / 5.0);
        float speed = mix(1.35, 0.65, fi / 5.0);

        slope += detailWaveSlope(xz, dir, wavelength, amplitude, speed, t);
    }

    vec2 crossWind = normalize(windRot * vec2(0.0, 1.0));
    slope += detailWaveSlope(xz * 1.15, crossWind, 3.0, 0.010, 0.85, t);
    slope += detailWaveSlope(xz * 0.85, normalize(crossWind + windRot * vec2(1.0, 0.0)), 5.5, 0.008, 0.75, t);

    return slope;
}

void main()
{
    vec2 xz = vWorldPos.xz;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Directional sun light (pointing *from* surface *towards* the sun)
    vec3 L = normalize(vec3(0.3, 1.0, 0.2));
    vec3 H = normalize(L + V);

    vec2 detailSlope = compositeDetailSlope(xz, uTime, uWindAngleRad);
    N = normalize(vec3(
        N.x - 0.55 * detailSlope.x,
        N.y,
        N.z - 0.55 * detailSlope.y
    ));

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
    vec3 F0 = vec3(0.02);
    vec3 F = fresnelSchlick(NdotV, F0);

    float slope = 1.0 - NdotV;              // grazing => more blur
    float roughness = mix(0.035, 0.18, slope);
    float maxLod = 6.0;              // depends on cubemap size (512 => ~9 mips, 6 is safe)

    // Reflection direction for environment lookup
    vec3 R = reflect(-V, N);
    vec3 env = textureLod(uSkybox, R, roughness * maxLod).rgb;

    if (uDebugMode == 3) {
        fragColor = vec4(env, 1.0);
        return;
    }

    vec3 reflection = env * (F * 1.3);

    float NdotH = max(dot(N, H), 0.0);
    vec3 FH = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = distributionGGX(NdotH, roughness);
    float Gs = geometrySmith(NdotV, NdotL, roughness);
    vec3 specular = (D * Gs * FH) / max(4.0 * NdotV * NdotL, 1e-4);
    specular *= NdotL * 1.6;

    // Combine:
    // - Reflection dominates at grazing angles via Fresnel
    // - Diffuse provides body color
    // - Specular adds sun glints
    vec3 color = diffuse * (1.0 - F) + reflection + specular;
    fragColor = vec4(color, 1.0);
}
