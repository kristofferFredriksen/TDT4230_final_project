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
    Mesh grid = makeGrid(/*n=*/256, /*size=*/300.0f);
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

    // Send uniforms (do NOT rely on fixed locations like 3)
    shader->activate();

    // GLint locTime = glGetUniformLocation(shader->getProgramID(), "uTime");
    // GLint locView = glGetUniformLocation(shader->getProgramID(), "uView");
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
}

void renderOcean(GLFWwindow* window)
{
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader->activate();
    glBindVertexArray(oceanVAO);

    if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawElements(GL_TRIANGLES, oceanIndexCount, GL_UNSIGNED_INT, nullptr);
    if (gWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}