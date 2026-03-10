#version 430 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

layout(location=0) uniform mat4 uView;
layout(location=1) uniform mat4 uProj;
layout(location=2) uniform float uTime;
layout(location=3) uniform vec3 uCameraPos; // optional for frag
layout(location=4) uniform int   uWaveCount;
layout(location=5) uniform float uAmplitudeScale;
layout(location=6) uniform float uSteepnessScale;
layout(location=7) uniform float uWindAngleRad;

// For simplicity: fixed max count
const int MAX_WAVES = 24;
layout(location=8) uniform vec4  uWaveDirAmp[MAX_WAVES]; // (dirx, dirz, amplitude, steepness)
layout(location=72) uniform vec3 uWaveLenSpeed[MAX_WAVES]; // (wavelength, speed, unused)

out vec3 vNormal;
out vec3 vWorldPos;

const float PI = 3.14159265359;

void gerstnerWave(
    in vec2 waveDirectionXZ,
    in float amplitude,
    in float wavelength,
    in float phaseSpeed,
    in float steepness,
    in vec2 xz,
    in float t, // Time in seconds
    inout vec3 disp, // Output: vertical displacement
    inout vec3 dPdx, // Output: partial derivative of displacement w.r.t x
    inout vec3 dPdz // Output: partial derivative of displacement w.r.t z
) {
    // Wave number k = 2π/λ
    float waveNumber = 2.0 * PI / wavelength;
    // Angular frequency ω = k * c
    float frequency = waveNumber *  phaseSpeed;
     // Phase of the wave at position xz and time t
    float phase = waveNumber * dot(waveDirectionXZ, xz) - frequency * t;
    float cosPhase = cos(phase);
    float sinPhase = sin(phase);
    float waveBand = clamp((wavelength - 3.0) / 24.0, 0.0, 1.0);
    float steepnessBoost = mix(0.72, 1.28, pow(waveBand, 0.65));
    float effectiveSteepness = clamp(steepness * steepnessBoost, 0.0, 1.35);

    // Sharpen larger and mid-scale waves so crests look less sinusoidal.
    float crestBlend = smoothstep(0.18, 0.82, waveBand);
    float sharpenedSin = sign(sinPhase) * pow(abs(sinPhase), mix(1.0, 1.65, crestBlend));
    float verticalProfile = mix(sinPhase, sharpenedSin, crestBlend);
    float verticalDerivative = cosPhase;
    if (crestBlend > 0.0) {
        float absSin = max(abs(sinPhase), 1e-4);
        float power = mix(1.0, 1.65, crestBlend);
        float shapedDerivative = power * pow(absSin, power - 1.0) * cosPhase;
        verticalDerivative = mix(cosPhase, shapedDerivative, crestBlend);
    }

    // Gerstner displacement
    disp.x += effectiveSteepness * amplitude * waveDirectionXZ.x * cosPhase;
    disp.y += amplitude * verticalProfile;
    disp.z += effectiveSteepness * amplitude * waveDirectionXZ.y * cosPhase;

    // Partial derivatives for normal calculation
    // Phase = k * (Dx*x + Dz*z) - ω*t
    // dPhase/dx = k * Dx
    // dPhase/dz = k * Dz
    float dPhase_dx = waveNumber * waveDirectionXZ.x;
    float dPhase_dz = waveNumber * waveDirectionXZ.y;

    // d/dx of the displaced position components
    dPdx.x += effectiveSteepness * amplitude * waveDirectionXZ.x * (-sinPhase) * dPhase_dx;
    dPdx.y += amplitude * verticalDerivative * dPhase_dx;
    dPdx.z += effectiveSteepness * amplitude * waveDirectionXZ.y * (-sinPhase) * dPhase_dx;

    // d/dz of the displaced position components
    dPdz.x += effectiveSteepness * amplitude * waveDirectionXZ.x * (-sinPhase) * dPhase_dz;
    dPdz.y += amplitude * verticalDerivative * dPhase_dz;
    dPdz.z += effectiveSteepness * amplitude * waveDirectionXZ.y * (-sinPhase) * dPhase_dz;
}

mat2 rotation2D(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat2(c, -s, s, c);
}

void main() {

    // Object-space position from grid
    vec3 basePos = aPos;
    
    //Accumulate Gerstner wave displacements
    vec3 totDisp = vec3(0.0);

    // Start derivatives 
    vec3 dPdx = vec3(1.0 , 0.0, 0.0);
    vec3 dPdz = vec3(0.0, 0.0, 1.0);

    vec2 xz = basePos.xz;
    float t = uTime; // Time in seconds

    mat2 windRot = rotation2D(uWindAngleRad);

    int count = clamp(uWaveCount, 0, MAX_WAVES);

    for (int i = 0; i < count; i++) {
        // Packed wave params
        vec2 dir = normalize(uWaveDirAmp[i].xy);
        float amp = uWaveDirAmp[i].z * uAmplitudeScale;
        float steep = uWaveDirAmp[i].w * uSteepnessScale;
        steep = clamp(steep, 0.0, 1.0);

        float wavelength = uWaveLenSpeed[i].x;
        float speed      = uWaveLenSpeed[i].y;

        // Apply global wind rotation to all directions
        dir = normalize(windRot * dir);

        gerstnerWave(dir, amp, wavelength, speed, steep, xz, t, totDisp, dPdx, dPdz);
    }
    // Final displaced position
    vec3 displacedPosition = basePos + totDisp;
    vWorldPos = displacedPosition;

    // Analytic normal from the two tangent directions.
    // cross(dP/dz, dP/dx) gives a consistent upward-facing normal for this coordinate setup.
    vec3 analyticNormal = normalize(cross(dPdz, dPdx));
    vNormal = analyticNormal;

    // Project to clip space
    gl_Position = uProj * uView * vec4(displacedPosition, 1.0);
}
