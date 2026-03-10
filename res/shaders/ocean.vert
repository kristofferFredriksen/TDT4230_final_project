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
layout(location=72) uniform vec3 uWaveLenSpeed[MAX_WAVES]; // (wavelength, speed, phaseOffset)

out vec3 vNormal;
out vec3 vWorldPos;
out float vBreak;
out float vCrest;
out float vBreakShape;

const float PI = 3.14159265359;

float hash11(float p)
{
    return fract(sin(p * 127.1) * 43758.5453123);
}

void gerstnerWave(
    in vec2 waveDirectionXZ,
    in float amplitude,
    in float wavelength,
    in float phaseSpeed,
    in float phaseOffset,
    in float steepness,
    in vec2 xz,
    in float t, // Time in seconds
    inout vec3 disp, // Output: vertical displacement
    inout vec3 dPdx, // Output: partial derivative of displacement w.r.t x
    inout vec3 dPdz, // Output: partial derivative of displacement w.r.t z
    inout float breakMask,
    inout float breakShape
) {
    // Wave number k = 2π/λ
    float waveNumber = 2.0 * PI / wavelength;
    // Angular frequency ω = k * c
    float frequency = waveNumber *  phaseSpeed;
     // Phase of the wave at position xz and time t
    float phase = waveNumber * dot(waveDirectionXZ, xz) - frequency * t + phaseOffset;
    float cosPhase = cos(phase);
    float sinPhase = sin(phase);
    float waveBand = clamp((wavelength - 3.0) / 24.0, 0.0, 1.0);
    float waveSeed = hash11(phaseOffset * 0.173 + wavelength * 0.071);
    float asymmetryJitter = mix(0.88, 1.22, waveSeed);
    vec2 crossDir = vec2(-waveDirectionXZ.y, waveDirectionXZ.x);
    float groupPhase = dot(xz, waveDirectionXZ) * (0.05 + 0.03 * waveSeed)
                     + dot(xz, crossDir) * (0.018 + 0.018 * (1.0 - waveSeed))
                     - t * (0.08 + 0.06 * waveSeed)
                     + phaseOffset * 0.37;
    float localEnvelope = mix(0.82, 1.18, 0.5 + 0.5 * sin(groupPhase));

    amplitude *= localEnvelope;

    float nominalSteepness = amplitude * waveNumber * steepness;
    float preBreak = smoothstep(0.56, 0.92, nominalSteepness);
    float steepnessBoost = mix(0.84, 1.02, pow(waveBand, 0.65));
    float effectiveSteepness = clamp(steepness * steepnessBoost, 0.0, 0.82);
    effectiveSteepness *= mix(1.0, 0.88, preBreak);

    // Keep some asymmetry for natural swell, but back off crest sharpening.
    float crestBlend = smoothstep(0.28, 0.96, waveBand) * (1.0 - 0.82 * preBreak);
    float crestSharpness = mix(1.0, 1.75, crestBlend) * asymmetryJitter;
    float minExp = exp(-crestSharpness);
    float maxExp = exp( crestSharpness);
    float expSin = exp(crestSharpness * sinPhase);
    float normalizedExpSin = (expSin - minExp) / max(maxExp - minExp, 1e-4);
    float asymmetricProfile = normalizedExpSin * 2.0 - 1.0;
    float asymmetricDerivative = (2.0 * crestSharpness * expSin * cosPhase) / max(maxExp - minExp, 1e-4);

    float verticalProfile = mix(sinPhase, asymmetricProfile, crestBlend);
    float verticalDerivative = mix(cosPhase, asymmetricDerivative, crestBlend);

    float crestPhase = smoothstep(0.42, 0.98, verticalProfile);
    breakMask += (0.14 + 0.42 * preBreak) * crestPhase * mix(0.25, 0.70, waveBand);
    breakShape += preBreak * smoothstep(0.76, 0.98, verticalProfile) * mix(0.18, 0.55, waveBand);

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
    float breakAccum = 0.0;
    float breakShapeAccum = 0.0;

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
        float phaseOffset = uWaveLenSpeed[i].z;

        // Apply global wind rotation to all directions
        dir = normalize(windRot * dir);

        gerstnerWave(dir, amp, wavelength, speed, phaseOffset, steep, xz, t, totDisp, dPdx, dPdz, breakAccum, breakShapeAccum);
    }
    // Final displaced position
    vec3 displacedPosition = basePos + totDisp;
    vWorldPos = displacedPosition;

    // Analytic normal from the two tangent directions.
    // cross(dP/dz, dP/dx) gives a consistent upward-facing normal for this coordinate setup.
    vec3 analyticNormal = normalize(cross(dPdz, dPdx));
    vNormal = analyticNormal;
    vBreak = clamp(breakAccum / max(float(count) * 0.55, 1.0), 0.0, 1.0);
    vCrest = smoothstep(0.88, 0.985, 1.0 - analyticNormal.y) * smoothstep(0.10, 0.70, totDisp.y);
    vBreakShape = clamp(breakShapeAccum / max(float(count) * 0.34, 1.0), 0.0, 1.0);

    // Project to clip space
    gl_Position = uProj * uView * vec4(displacedPosition, 1.0);
}
