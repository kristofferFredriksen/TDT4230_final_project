#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <utilities/shader.hpp>
#include <utilities/timeutils.h>
#include <utilities/mesh.h>
#include <utilities/glutils.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cmath>
#include <iostream>

#include "ocean.hpp"

// --------------------
// Minimal state
// --------------------
static Gloom::Shader* shader = nullptr;
static unsigned int oceanVAO = 0;
static int oceanIndexCount = 0;
static float gOceanSize = 3000.0f;
static int gResolution = 4096; // or 2048 for more detail (beware perf!)

static float timeSeconds = 0.0f;

// Orbit camera state
static float camYaw   = 0.0f;
static float camPitch = 0.35f;
static float camRadius = 60.0f;

static bool dragging = false;
static double lastMouseX = 0.0;
static double lastMouseY = 0.0;

static bool gWireframe = true;
static bool gPauseTime = false;

static glm::mat4 gLastView(1.0f);
static glm::mat4 gLastProj(1.0f);

// --------------------
// Input helper
// --------------------

static bool keyPressed(GLFWwindow* window, int key)
{
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    static bool prev[GLFW_KEY_LAST + 1] = { false };
    bool down = (glfwGetKey(window, key) == GLFW_PRESS);
    bool pressed = down && !prev[key];
    prev[key] = down;
    return pressed;
}

struct WaveParams {
    glm::vec2 directionXZ;  // normalized
    float amplitude;
    float wavelength;
    float speed;
    float steepness;
};

static std::vector<WaveParams> gWaves = {
    // Dominant wind-driven swell, with one weaker secondary system to avoid uniformity.
    { glm::normalize(glm::vec2(1.0f,  0.16f)), 1.95f, 74.0f, 0.0f, 0.76f },
    { glm::normalize(glm::vec2(0.98f, -0.18f)), 1.46f, 56.0f, 0.0f, 0.70f },
    { glm::normalize(glm::vec2(0.90f, 0.43f)), 1.02f, 40.0f, 0.0f, 0.60f },
    { glm::normalize(glm::vec2(-0.30f, 0.95f)), 0.50f, 30.0f, 0.0f, 0.43f },
};

static constexpr int MAX_WAVES = 24;
static constexpr int SWELL_WAVES = 4;
static constexpr int MID_WAVES = 8;

static int gWaveCount = MAX_WAVES; // or 8
static float gAmplitudeScale  = 0.513158f;
static float gSteepnessScale  = 1.0f;
static float gWindAngleRad    = 0.0f;  // rotates all directions
static int   gDebugMode       = 0;     // 0 shaded, 1 normals (optional)
static glm::vec3 gSunDir      = glm::normalize(glm::vec3(-0.36f, 0.52f, 0.78f));
static glm::vec3 gSunColor    = glm::vec3(1.0f, 0.93f, 0.82f);
static float gSunIntensity    = 1.35f;

static float deepWaterPhaseSpeed(float wavelength)
{
    constexpr float g = 9.81f;
    constexpr float twoPi = 6.28318530718f;
    float k = twoPi / wavelength;
    return std::sqrt(g / k);
}

static float waveGroupEnvelope(float time, float frequency, float phase, float strength)
{
    float pulse = 0.5f + 0.5f * std::sin(time * frequency + phase);
    return glm::mix(1.0f - strength, 1.0f + strength, pulse);
}

static float pseudoRandom01(int seed)
{
    float x = std::sin(float(seed) * 127.1f + 311.7f) * 43758.5453f;
    return x - std::floor(x);
}

// --------------------
// Grid generator (XZ plane)
// --------------------
static Mesh makeGrid(int n, float size)
{
    Mesh m;

    // (n+1) x (n+1) vertices
    const int vertsPerSide = n + 1;
    m.vertices.reserve(vertsPerSide * vertsPerSide);
    m.normals.reserve(vertsPerSide * vertsPerSide);

    for (int z = 0; z < vertsPerSide; z++) {
        for (int x = 0; x < vertsPerSide; x++) {
            float u = float(x) / float(n);
            float v = float(z) / float(n);

            float px = (u - 0.5f) * size;
            float pz = (v - 0.5f) * size;

            m.vertices.emplace_back(px, 0.0f, pz);
            m.normals.emplace_back(0.0f, 1.0f, 0.0f);
        }
    }

    // indices: 2 triangles per cell
    m.indices.reserve(n * n * 6);
    auto idx = [vertsPerSide](int x, int z) { return z * vertsPerSide + x; };

    for (int z = 0; z < n; z++) {
        for (int x = 0; x < n; x++) {
            unsigned int i0 = idx(x, z);
            unsigned int i1 = idx(x + 1, z);
            unsigned int i2 = idx(x, z + 1);
            unsigned int i3 = idx(x + 1, z + 1);

            // Winding should match your default; if it renders “inside out”, swap i1/i2.
            m.indices.push_back(i0); m.indices.push_back(i2); m.indices.push_back(i1);
            m.indices.push_back(i1); m.indices.push_back(i2); m.indices.push_back(i3);
        }
    }

    return m;
}

// --------------------
// Input helpers
// --------------------
static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        dragging = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    }
}

static void cursorPosCallback(GLFWwindow* window, double x, double y)
{
    if (!dragging) return;

    double dx = x - lastMouseX;
    double dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;

    const float sens = 0.005f;
    camYaw   += float(dx) * sens;
    camPitch += float(dy) * sens;

    // clamp pitch
    camPitch = glm::clamp(camPitch, -1.2f, 1.2f);
}

void initOcean(GLFWwindow* window, CommandLineOptions options)
{
    // Basic GL state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Hook minimal input
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);

    // Shader
    shader = new Gloom::Shader();
    shader->makeBasicShader("../res/shaders/ocean.vert", "../res/shaders/ocean.frag");
    shader->activate();

    // Grid mesh
    Mesh grid = makeGrid(/*n=*/gResolution, /*size=*/gOceanSize);
    oceanVAO = generateBuffer(grid);
    oceanIndexCount = int(grid.indices.size());

    // Reset delta timer
    getTimeDeltaSeconds();
}

void updateOcean(GLFWwindow* window)
{
    float dt = float(getTimeDeltaSeconds());

    // Toggle wireframe
    if (keyPressed(window, GLFW_KEY_F1)) {
        gWireframe = !gWireframe;
        std::cout << "[F1] Wireframe: " << (gWireframe ? "ON" : "OFF") << std::endl;
    }

    if (keyPressed(window, GLFW_KEY_F2)) {
        gDebugMode = (gDebugMode + 1) % 5;
        std::cout << "DebugMode " << gDebugMode << "\n";
    }

    // Toggle pause
    if (keyPressed(window, GLFW_KEY_SPACE)) {
        gPauseTime = !gPauseTime;
        std::cout << "[SPACE] Pause: " << (gPauseTime ? "ON" : "OFF") << std::endl;
    }

    if (!gPauseTime) {
        timeSeconds += dt;
    }

    // Keyboard tuning (optional baseline)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camRadius -= 40.0f * dt;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camRadius += 40.0f * dt;
    camRadius = glm::clamp(camRadius, 10.0f, 400.0f);

    // Interactivity: press once per tap
    if (keyPressed(window, GLFW_KEY_UP))    { gAmplitudeScale *= 1.1f; std::cout << "AmplitudeScale " << gAmplitudeScale << "\n"; }
    if (keyPressed(window, GLFW_KEY_DOWN))  { gAmplitudeScale /= 1.1f; std::cout << "AmplitudeScale " << gAmplitudeScale << "\n"; }

    if (keyPressed(window, GLFW_KEY_RIGHT)) { gSteepnessScale *= 1.1f; std::cout << "SteepnessScale " << gSteepnessScale << "\n"; }
    if (keyPressed(window, GLFW_KEY_LEFT))  { gSteepnessScale /= 1.1f; std::cout << "SteepnessScale " << gSteepnessScale << "\n"; }

    if (keyPressed(window, GLFW_KEY_PAGE_UP)) {
        gWaveCount = std::min(gWaveCount + 1, MAX_WAVES);
        std::cout << "WaveCount " << gWaveCount << "\n";
    }
    if (keyPressed(window, GLFW_KEY_PAGE_DOWN)) {
        gWaveCount = std::max(gWaveCount - 1, 1);
        std::cout << "WaveCount " << gWaveCount << "\n";
    }

    if (keyPressed(window, GLFW_KEY_COMMA)) { gWindAngleRad -= 0.1f; std::cout << "WindAngle " << gWindAngleRad << "\n"; }
    if (keyPressed(window, GLFW_KEY_PERIOD)){ gWindAngleRad += 0.1f; std::cout << "WindAngle " << gWindAngleRad << "\n"; }

    // Camera orbit calculation
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::vec3 camPos;
    camPos.x = target.x + camRadius * std::cos(camPitch) * std::sin(camYaw);
    camPos.y = target.y + camRadius * std::sin(camPitch);
    camPos.z = target.z + camRadius * std::cos(camPitch) * std::cos(camYaw);

    glm::mat4 view = glm::lookAt(camPos, target, glm::vec3(0, 1, 0));

    int w, h;
    glfwGetWindowSize(window, &w, &h);
    float aspect = (h > 0) ? float(w) / float(h) : 1.0f;

    glm::mat4 proj = glm::perspective(glm::radians(55.0f), aspect, 0.1f, 1000.0f);

    gLastView = view;
    gLastProj = proj;

    // Send uniforms (do NOT rely on fixed locations like 3)
    shader->activate();

    GLint locSky = glGetUniformLocation(shader->get(), "uSkybox");
    glUniform1i(locSky, 0);

    glUniform1i(400, gDebugMode);

    // GLint locTime = glGetUniformLocation(shader->getProgramID(), "uTime");
    // GLint locView = gstatic int gWaveCount = MAX_WAVES; // or 8lGetUniformLocation(shader->getProgramID(), "uView");
    // GLint locProj = glGetUniformLocation(shader->getProgramID(), "uProj");
    // GLint locCam  = glGetUniformLocation(shader->getProgramID(), "uCameraPos");

    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(view));   // uView
    glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(proj));   // uProj
    glUniform1f(2, timeSeconds);                                // uTime
    glUniform3fv(3, 1, glm::value_ptr(camPos));                 // uCameraPos

    // if (locTime >= 0) glUniform1f(locTime, timeSeconds);
    // if (locView >= 0) glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
    // if (locProj >= 0) glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(proj));
    // if (locCam  >= 0) glUniform3fv(locCam, 1, glm::value_ptr(camPos));

    // Later: set wave arrays here (amplitude, dir, wavelength, speed, steepness)
    glUniform1i(4, gWaveCount);
    glUniform1f(5, gAmplitudeScale);
    glUniform1f(6, gSteepnessScale);
    glUniform1f(7, gWindAngleRad);
    glUniform2f(401, 0.5f * gOceanSize, 0.5f * gOceanSize);
    glUniform3fv(402, 1, glm::value_ptr(gSunDir));
    glUniform3fv(403, 1, glm::value_ptr(gSunColor));
    glUniform1f(404, gSunIntensity);

    // Pack into arrays
    glm::vec4 dirAmp[MAX_WAVES];
    glm::vec3 lenSpeed[MAX_WAVES];

    for (int i = 0; i < MAX_WAVES; i++) {
        if (i < SWELL_WAVES && i < (int)gWaves.size()) {
            // Swell waves (hand-tuned)
            float envelope = waveGroupEnvelope(timeSeconds, 0.11f + 0.015f * float(i), 0.9f * float(i), 0.18f);
            float phase = glm::two_pi<float>() * pseudoRandom01(17 + i * 13);
            dirAmp[i]   = glm::vec4(gWaves[i].directionXZ.x, gWaves[i].directionXZ.y,
                                    gWaves[i].amplitude * envelope, gWaves[i].steepness);
            lenSpeed[i] = glm::vec3(gWaves[i].wavelength, deepWaterPhaseSpeed(gWaves[i].wavelength), phase);
        } else if (i < SWELL_WAVES + MID_WAVES) {
            float idx = float(i - SWELL_WAVES);
            float t = idx / float(std::max(MID_WAVES - 1, 1));
            float rand = pseudoRandom01(101 + i * 19);
            float spread = glm::mix(-0.50f, 0.50f, t) + glm::mix(-0.16f, 0.16f, rand);
            if ((i & 1) == 1) spread += 0.20f * (rand - 0.5f);
            glm::vec2 dir = glm::normalize(glm::vec2(std::cos(spread), std::sin(spread)));

            float wavelength = glm::mix(34.0f, 10.0f, std::pow(t, 0.82f)) * glm::mix(0.82f, 1.24f, rand);
            float amplitude  = glm::mix(0.50f, 0.12f, std::pow(t, 0.72f)) * glm::mix(0.72f, 1.24f, rand);
            float steepness  = glm::mix(0.66f, 0.28f, t) * glm::mix(0.82f, 1.12f, 1.0f - rand);
            float speed      = deepWaterPhaseSpeed(wavelength);
            float envelope   = waveGroupEnvelope(timeSeconds, 0.17f + 0.05f * float(i % 3), 0.7f * idx, 0.22f);
            float phase      = glm::two_pi<float>() * pseudoRandom01(211 + i * 23);

            dirAmp[i]   = glm::vec4(dir.x, dir.y, amplitude * envelope, steepness);
            lenSpeed[i] = glm::vec3(wavelength, speed, phase);
        } else {
            float idx = float(i - SWELL_WAVES - MID_WAVES);
            float t = idx / float(std::max(MAX_WAVES - SWELL_WAVES - MID_WAVES - 1, 1));
            float rand = pseudoRandom01(307 + i * 29);
            float spread = glm::mix(-0.95f, 0.95f, t) + glm::mix(-0.26f, 0.26f, rand);
            if (i >= MAX_WAVES - 3) spread += (i == MAX_WAVES - 1 ? 0.95f : -0.70f);

            glm::vec2 dir = glm::normalize(glm::vec2(std::cos(spread), std::sin(spread)));

            float wavelength = glm::mix(10.5f, 2.6f, std::pow(t, 0.86f)) * glm::mix(0.72f, 1.32f, rand);
            float amplitude  = glm::mix(0.10f, 0.018f, std::pow(t, 0.74f)) * glm::mix(0.70f, 1.30f, rand);
            float steepness  = glm::mix(0.30f, 0.09f, t) * glm::mix(0.80f, 1.18f, 1.0f - rand);
            float speed      = deepWaterPhaseSpeed(wavelength);
            float envelope   = waveGroupEnvelope(timeSeconds, 0.28f + 0.05f * float(i % 4), 0.45f * idx, 0.12f);
            float phase      = glm::two_pi<float>() * pseudoRandom01(401 + i * 31);

            dirAmp[i]   = glm::vec4(dir.x, dir.y, amplitude * envelope, steepness);
            lenSpeed[i] = glm::vec3(wavelength, speed, phase);
        }
    }

    glUniform4fv(8,  MAX_WAVES, glm::value_ptr(dirAmp[0]));
    glUniform3fv(72, MAX_WAVES, glm::value_ptr(lenSpeed[0]));
}

const glm::mat4& getOceanView() { return gLastView; }
const glm::mat4& getOceanProj() { return gLastProj; }

void renderOcean(GLFWwindow* window)
{
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);

    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader->activate();
    glBindVertexArray(oceanVAO);

    if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, oceanIndexCount, GL_UNSIGNED_INT, nullptr);
    if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}
