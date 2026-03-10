#version 430 core

in vec3 vNormal;
in vec3 vWorldPos;
in float vBreak;
in float vCrest;
in float vBreakShape;

layout(location=2) uniform float uTime;
layout(location=3) uniform vec3 uCameraPos;
layout(location=7) uniform float uWindAngleRad;
layout(location=400) uniform int uDebugMode;
layout(location=401) uniform vec2 uOceanHalfExtent;

out vec4 fragColor;

const float PI = 3.14159265358979323846;
const float G  = 9.81;

uniform samplerCube uSkybox;

float hash12(vec2 p);
float valueNoise(vec2 p);

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    // Schlick approximation
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float exponentialFog(float distanceToCamera, float density)
{
    float fog = 1.0 - exp(-(distanceToCamera * density) * (distanceToCamera * density));
    return clamp(fog, 0.0, 1.0);
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
    vec2 windDir = normalize(windRot * vec2(1.0, 0.0));
    vec2 crossWind = vec2(-windDir.y, windDir.x);

    // Keep a dominant wind-driven detail field underneath.
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        float ratio = fi / 4.0;
        float spread = mix(-0.42, 0.42, ratio);
        vec2 dir = normalize(windRot * vec2(cos(spread), sin(spread)));

        float wavelength = mix(1.8, 9.5, pow(ratio, 1.05));
        float amplitude = mix(0.014, 0.034, 1.0 - ratio);
        float speed = mix(1.35, 0.72, ratio);

        slope += detailWaveSlope(xz, dir, wavelength, amplitude, speed, t);
    }

    // Layer patchy secondary ripples on top so details vary without losing the wind direction.
    float patchA = valueNoise(xz * 0.030 + vec2(t * 0.06, -t * 0.03));
    float patchB = valueNoise(xz.yx * 0.045 + vec2(-t * 0.04, t * 0.05));
    vec2 obliqueA = normalize(windDir + 0.75 * crossWind);
    vec2 obliqueB = normalize(windDir - 0.95 * crossWind);
    vec2 reverseWind = normalize(-windDir + 0.35 * crossWind);

    slope += detailWaveSlope(xz * 1.10, obliqueA, 4.2, 0.011 * patchA, 0.95, t);
    slope += detailWaveSlope(xz * 0.86, obliqueB, 3.1, 0.010 * patchB, 1.10, t);
    slope += detailWaveSlope(xz * 1.18, reverseWind, 5.8, 0.006 * smoothstep(0.55, 0.95, patchA), 0.72, t);

    return slope;
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float breakFoamMask(vec2 xz, float t, float windAngle, float crestMask, float breakShape)
{
    mat2 windRot = rotation2D(windAngle);
    vec2 windDir = normalize(windRot * vec2(1.0, 0.0));
    vec2 crossDir = vec2(-windDir.y, windDir.x);
    vec2 advected = xz * 0.085 + windDir * (t * 0.14);

    // Follow the actual wave crest shape first, then add only mild along-crest breakup.
    float alongCrestVariation = valueNoise(vec2(dot(xz, windDir) * 0.07 - t * 0.05, dot(xz, crossDir) * 0.025));
    float breakup = valueNoise(advected + crossDir * 0.7);
    float mask = breakShape * mix(0.82, 1.0, alongCrestVariation) * mix(0.88, 1.0, breakup);
    return smoothstep(0.24, 0.62, mask) * crestMask;
}

void main()
{
    vec2 xz = vWorldPos.xz;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Directional sun light (pointing *from* surface *towards* the sun)
    vec3 L = normalize(vec3(-0.36, 0.52, 0.78));
    vec3 H = normalize(L + V);

    vec2 detailSlope = compositeDetailSlope(xz, uTime, uWindAngleRad);
    N = normalize(vec3(N.x - 0.28 * detailSlope.x, N.y, N.z - 0.28 * detailSlope.y));

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Debug toggles you can later control with a uniform
    if (uDebugMode == 1) {
        fragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }

    float breakFoam = clamp((0.01 + 0.45 * vBreak) * breakFoamMask(xz, uTime, uWindAngleRad, vCrest, vBreakShape), 0.0, 1.0);

    if (uDebugMode == 4) {
        fragColor = vec4(vec3(breakFoam), 1.0);
        return;
    }

    // Water color varies with apparent depth, wave shape, and light alignment.
    vec3 deepColor   = vec3(0.015, 0.075, 0.125);
    vec3 bodyColor   = vec3(0.030, 0.165, 0.210);
    vec3 crestColor  = vec3(0.110, 0.320, 0.340);
    vec3 troughColor = vec3(0.010, 0.055, 0.095);

    float distanceToCamera = length(uCameraPos - vWorldPos);
    float density = 0.015;
    float depthFactor = clamp(1.0 - exp(-distanceToCamera * density), 0.0, 1.0);

    if (uDebugMode == 2) {
        fragColor = vec4(vec3(depthFactor), 1.0);
        return;
    }

    float crestMask = smoothstep(0.12, 1.15, vWorldPos.y);
    float troughMask = smoothstep(-0.90, -0.10, -vWorldPos.y);
    float slopeMask = clamp(1.0 - N.y, 0.0, 1.0);
    float facingLight = pow(max(dot(normalize(N.xz + vec2(1e-4)), normalize(L.xz)), 0.0), 1.8);

    vec3 waterColor = mix(bodyColor, deepColor, depthFactor);
    waterColor = mix(waterColor, troughColor, 0.35 * troughMask + 0.25 * slopeMask);
    waterColor = mix(waterColor, crestColor, 0.45 * crestMask + 0.18 * facingLight * slopeMask);

    float subsurface = (0.14 + 0.32 * crestMask + 0.24 * facingLight) * (1.0 - depthFactor * 0.50);
    vec3 diffuse = waterColor * (0.08 + 0.24 * NdotL) + vec3(0.030, 0.090, 0.075) * subsurface;

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

    vec3 reflection = env * (F * 1.45);

    float NdotH = max(dot(N, H), 0.0);
    vec3 FH = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = distributionGGX(NdotH, roughness);
    float Gs = geometrySmith(NdotV, NdotL, roughness);
    vec3 specular = (D * Gs * FH) / max(4.0 * NdotV * NdotL, 1e-4);
    specular *= NdotL * 2.35;

    vec3 horizonTint = vec3(0.030, 0.080, 0.095) * pow(1.0 - NdotV, 2.4);
    vec3 breakFoamColor = vec3(0.87, 0.90, 0.89);
    reflection *= (1.0 - 0.55 * breakFoam);
    specular *= (1.0 - 0.75 * breakFoam);

    // Combine:
    // - Reflection dominates at grazing angles via Fresnel
    // - Diffuse provides body color
    // - Specular adds sun glints
    vec3 color = diffuse * (1.0 - F) + reflection + specular + horizonTint;
    color = mix(color, breakFoamColor, breakFoam * 0.48);

    vec3 viewDir = normalize(vWorldPos - uCameraPos);
    vec3 fogDir = normalize(vec3(viewDir.x, max(viewDir.y, -0.08), viewDir.z));
    vec3 fogColor = textureLod(uSkybox, fogDir, 1.5).rgb;
    vec2 edgeRatio = abs(vWorldPos.xz) / max(uOceanHalfExtent, vec2(1e-3));
    float meshEdge = max(edgeRatio.x, edgeRatio.y);
    float edgeFog = smoothstep(0.84, 0.985, meshEdge);
    float horizonMask = pow(clamp(1.0 - abs(viewDir.y), 0.0, 1.0), 3.0);
    float horizonFog = edgeFog * mix(0.70, 1.0, horizonMask);
    color = mix(color, fogColor, clamp(horizonFog, 0.0, 0.88));

    fragColor = vec4(color, 1.0);
}
