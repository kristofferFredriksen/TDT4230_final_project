#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <utilities/shader.hpp>
#include <utilities/timeutils.h>
#include <utilities/mesh.h>
#include <utilities/glutils.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_easy_font.h>

#include <vector>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "ocean.hpp"

// --------------------
// Minimal state
// --------------------
static Gloom::Shader* shader = nullptr;
static Gloom::Shader* guiShader = nullptr;
static unsigned int oceanVAO = 0;
static int oceanIndexCount = 0;
static float gOceanSize = 3000.0f;
static int gResolution = 4096; // or 2048 for more detail (beware perf!)
static unsigned int guiVAO = 0;
static unsigned int guiVBO = 0;

static float timeSeconds = 0.0f;

// Orbit camera state
static float camYaw   = 0.0f;
static float camPitch = 0.35f;
static float camRadius = 60.0f;

static bool dragging = false;
static double lastMouseX = 0.0;
static double lastMouseY = 0.0;
static int gActiveSlider = -1;

static bool gWireframe = true;
static bool gPauseTime = false;

static glm::mat4 gLastView(1.0f);
static glm::mat4 gLastProj(1.0f);

struct GuiVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

static constexpr float GUI_PANEL_X = 20.0f;
static constexpr float GUI_PANEL_Y = 20.0f;
static constexpr float GUI_PANEL_W = 280.0f;
static constexpr float GUI_PANEL_H = 224.0f;
static constexpr float GUI_SLIDER_X = 132.0f;
static constexpr float GUI_SLIDER_W = 128.0f;
static constexpr float GUI_ROW_H = 32.0f;
static constexpr int GUI_SLIDER_COUNT = 5;

static void appendRect(std::vector<GuiVertex>& vertices, float x, float y, float w, float h, const glm::vec4& color)
{
    GuiVertex v0 { x,     y,     color.r, color.g, color.b, color.a };
    GuiVertex v1 { x + w, y,     color.r, color.g, color.b, color.a };
    GuiVertex v2 { x + w, y + h, color.r, color.g, color.b, color.a };
    GuiVertex v3 { x,     y + h, color.r, color.g, color.b, color.a };
    vertices.push_back(v0); vertices.push_back(v1); vertices.push_back(v2);
    vertices.push_back(v0); vertices.push_back(v2); vertices.push_back(v3);
}

static void appendEasyFontText(std::vector<GuiVertex>& vertices, float x, float y, const std::string& text, const glm::vec4& color)
{
    char buffer[12000];
    unsigned char rgba[4] = {
        static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f)
    };
    int quadCount = stb_easy_font_print(x, y, const_cast<char*>(text.c_str()), rgba, buffer, sizeof(buffer));
    for (int i = 0; i < quadCount; ++i) {
        const unsigned char* base = reinterpret_cast<const unsigned char*>(buffer) + i * 4 * 16;
        GuiVertex quad[4];
        for (int j = 0; j < 4; ++j) {
            const float* pos = reinterpret_cast<const float*>(base + j * 16);
            const unsigned char* c = base + j * 16 + 12;
            quad[j] = {
                pos[0], pos[1],
                c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f
            };
        }
        vertices.push_back(quad[0]); vertices.push_back(quad[1]); vertices.push_back(quad[2]);
        vertices.push_back(quad[0]); vertices.push_back(quad[2]); vertices.push_back(quad[3]);
    }
}

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

static const char* guiSliderLabel(int index)
{
    switch (index) {
        case 0: return "Amplitude";
        case 1: return "Steepness";
        case 2: return "Wind Angle";
        case 3: return "Sun Power";
        case 4: return "Wave Count";
        default: return "";
    }
}

static bool guiConsumesCursor(double x, double y)
{
    return x >= GUI_PANEL_X && x <= GUI_PANEL_X + GUI_PANEL_W &&
           y >= GUI_PANEL_Y && y <= GUI_PANEL_Y + GUI_PANEL_H;
}

static int sliderIndexAtCursor(double x, double y)
{
    if (!guiConsumesCursor(x, y)) return -1;
    for (int i = 0; i < GUI_SLIDER_COUNT; ++i) {
        float rowTop = GUI_PANEL_Y + 36.0f + float(i) * GUI_ROW_H;
        float rowBottom = rowTop + 20.0f;
        float sliderLeft = GUI_PANEL_X + GUI_SLIDER_X;
        float sliderRight = sliderLeft + GUI_SLIDER_W;
        if (x >= sliderLeft && x <= sliderRight && y >= rowTop && y <= rowBottom) {
            return i;
        }
    }
    return -1;
}

static float sliderNormalizedFromCursor(double x)
{
    float sliderLeft = GUI_PANEL_X + GUI_SLIDER_X;
    float t = float((x - sliderLeft) / GUI_SLIDER_W);
    return glm::clamp(t, 0.0f, 1.0f);
}

static float normalizedFromRange(float value, float minValue, float maxValue)
{
    return glm::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

static void updateSliderValueFromCursor(int sliderIndex, double x)
{
    float t = sliderNormalizedFromCursor(x);
    switch (sliderIndex) {
        case 0: gAmplitudeScale = glm::mix(0.1f, 1.6f, t); break;
        case 1: gSteepnessScale = glm::mix(0.35f, 2.0f, t); break;
        case 2: gWindAngleRad = glm::mix(-glm::pi<float>(), glm::pi<float>(), t); break;
        case 3: gSunIntensity = glm::mix(0.4f, 2.6f, t); break;
        case 4: gWaveCount = glm::clamp(int(std::round(glm::mix(4.0f, float(MAX_WAVES), t))), 1, MAX_WAVES); break;
        default: break;
    }
}

static float currentSliderNormalized(int sliderIndex)
{
    switch (sliderIndex) {
        case 0: return normalizedFromRange(gAmplitudeScale, 0.1f, 1.6f);
        case 1: return normalizedFromRange(gSteepnessScale, 0.35f, 2.0f);
        case 2: return normalizedFromRange(gWindAngleRad, -glm::pi<float>(), glm::pi<float>());
        case 3: return normalizedFromRange(gSunIntensity, 0.4f, 2.6f);
        case 4: return normalizedFromRange(float(gWaveCount), 4.0f, float(MAX_WAVES));
        default: return 0.0f;
    }
}

static std::string currentSliderValueText(int sliderIndex)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    switch (sliderIndex) {
        case 0: out << gAmplitudeScale; break;
        case 1: out << gSteepnessScale; break;
        case 2: out << gWindAngleRad; break;
        case 3: out << gSunIntensity; break;
        case 4: out.str(""); out.clear(); out << gWaveCount; break;
        default: break;
    }
    return out.str();
}

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
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
        if (action == GLFW_PRESS) {
            gActiveSlider = sliderIndexAtCursor(lastMouseX, lastMouseY);
            dragging = (gActiveSlider < 0);
            if (gActiveSlider >= 0) {
                updateSliderValueFromCursor(gActiveSlider, lastMouseX);
            }
        } else {
            dragging = false;
            gActiveSlider = -1;
        }
    }
}

static void cursorPosCallback(GLFWwindow* window, double x, double y)
{
    if (gActiveSlider >= 0) {
        updateSliderValueFromCursor(gActiveSlider, x);
        lastMouseX = x;
        lastMouseY = y;
        return;
    }

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

    guiShader = new Gloom::Shader();
    guiShader->makeBasicShader("../res/shaders/gui.vert", "../res/shaders/gui.frag");

    glGenVertexArrays(1, &guiVAO);
    glGenBuffers(1, &guiVBO);
    glBindVertexArray(guiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, guiVBO);
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(GuiVertex), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GuiVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GuiVertex), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

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

void renderOceanGui(GLFWwindow* window)
{
    if (!guiShader || !guiVAO || !guiVBO) return;

    int width = 0;
    int height = 0;
    glfwGetWindowSize(window, &width, &height);

    std::vector<GuiVertex> vertices;
    vertices.reserve(4096);

    appendRect(vertices, GUI_PANEL_X, GUI_PANEL_Y, GUI_PANEL_W, GUI_PANEL_H, glm::vec4(0.05f, 0.08f, 0.11f, 0.78f));
    appendRect(vertices, GUI_PANEL_X + 1.0f, GUI_PANEL_Y + 1.0f, GUI_PANEL_W - 2.0f, 26.0f, glm::vec4(0.13f, 0.22f, 0.28f, 0.92f));
    appendEasyFontText(vertices, GUI_PANEL_X + 10.0f, GUI_PANEL_Y + 8.0f, "Ocean Controls", glm::vec4(0.95f, 0.97f, 1.0f, 1.0f));

    const glm::vec4 sliderColors[GUI_SLIDER_COUNT] = {
        {0.21f, 0.71f, 0.93f, 1.0f},
        {0.94f, 0.72f, 0.23f, 1.0f},
        {0.57f, 0.80f, 0.39f, 1.0f},
        {0.97f, 0.56f, 0.30f, 1.0f},
        {0.82f, 0.45f, 0.89f, 1.0f}
    };

    for (int i = 0; i < GUI_SLIDER_COUNT; ++i) {
        float rowTop = GUI_PANEL_Y + 36.0f + float(i) * GUI_ROW_H;
        float sliderLeft = GUI_PANEL_X + GUI_SLIDER_X;
        float t = currentSliderNormalized(i);
        float fillWidth = GUI_SLIDER_W * t;

        appendEasyFontText(vertices, GUI_PANEL_X + 10.0f, rowTop + 3.0f, guiSliderLabel(i), glm::vec4(0.90f, 0.93f, 0.96f, 1.0f));
        appendEasyFontText(vertices, GUI_PANEL_X + 222.0f, rowTop + 3.0f, currentSliderValueText(i), glm::vec4(0.80f, 0.86f, 0.90f, 1.0f));
        appendRect(vertices, sliderLeft, rowTop + 16.0f, GUI_SLIDER_W, 6.0f, glm::vec4(0.14f, 0.18f, 0.22f, 0.95f));
        appendRect(vertices, sliderLeft, rowTop + 16.0f, fillWidth, 6.0f, sliderColors[i]);
        appendRect(vertices, sliderLeft + fillWidth - 4.0f, rowTop + 13.0f, 8.0f, 12.0f, glm::vec4(0.98f, 0.99f, 1.0f, 1.0f));
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    guiShader->activate();
    glUniform2f(0, float(width), float(height));

    glBindVertexArray(guiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, guiVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GuiVertex), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
