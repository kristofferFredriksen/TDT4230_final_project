// Local headers
#include "program.hpp"
#include "utilities/window.hpp"
#include "ocean.hpp"

// System / GL
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Utilities
#include <utilities/shader.hpp>
#include <utilities/imageLoader.hpp> 
#include <iostream>
#include <vector>

static const float skyboxVertices[] = {
    -1,  1, -1,  -1, -1, -1,   1, -1, -1,   1, -1, -1,   1,  1, -1,  -1,  1, -1,
    -1, -1,  1,  -1, -1, -1,  -1,  1, -1,  -1,  1, -1,  -1,  1,  1,  -1, -1,  1,
     1, -1, -1,   1, -1,  1,   1,  1,  1,   1,  1,  1,   1,  1, -1,   1, -1, -1,
    -1, -1,  1,  -1,  1,  1,   1,  1,  1,   1,  1,  1,   1, -1,  1,  -1, -1,  1,
    -1,  1, -1,   1,  1, -1,   1,  1,  1,   1,  1,  1,  -1,  1,  1,  -1,  1, -1,
    -1, -1, -1,  -1, -1,  1,   1, -1, -1,   1, -1, -1,  -1, -1,  1,   1, -1,  1
};

static GLuint loadCubemap(const std::vector<std::string>& faces)
{
    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

    for (unsigned int i = 0; i < faces.size(); i++) {
        PNGImage img = loadPNGFile(faces[i]);

        if (img.pixels.empty() || img.width == 0 || img.height == 0) {
            std::cerr << "Cubemap face load failed: " << faces[i] << "\n";
            continue;
        }

        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
            0,
            GL_RGBA8,
            (GLsizei)img.width,
            (GLsizei)img.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            img.pixels.data()
        );
    }

    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    GLint w = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &w);
    std::cout << "Cubemap +X width: " << w << "\n";

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return texID;
}

void runProgram(GLFWwindow* window, CommandLineOptions options)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- Skybox setup (once) ---
    GLuint skyboxVAO = 0, skyboxVBO = 0;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);

    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    Gloom::Shader skyboxShader;
    skyboxShader.makeBasicShader("../res/shaders/skybox.vert", "../res/shaders/skybox.frag");

    // Order must be: +X, -X, +Y, -Y, +Z, -Z
    // From your template: +X=FRONT(PX), -X=BACK(NX), +Y=UP(PY), -Y=DOWN(NY), +Z=RIGHT(PZ), -Z=LEFT(NZ)
    std::vector<std::string> faces = {
        "../res/skybox/px.png", // +X (PX)
        "../res/skybox/nx.png",  // -X (NX)
        "../res/skybox/py.png",    // +Y (PY)
        "../res/skybox/ny.png",  // -Y (NY)
        "../res/skybox/pz.png", // +Z (PZ)
        "../res/skybox/nz.png"   // -Z (NZ)
    };
    GLuint skyboxTex = loadCubemap(faces);

    // Ocean init
    initOcean(window, options);

    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Update ocean (also updates view/proj uniforms for ocean shader)
        updateOcean(window);

        // --- Render skybox ---
        glm::mat4 view = getOceanView();
        glm::mat4 proj = getOceanProj();
        glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(view));

        // Save current state
        GLint oldDepthFunc;
        glGetIntegerv(GL_DEPTH_FUNC, &oldDepthFunc);
        GLboolean oldDepthMask;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
        GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);

        // Skybox state
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);            // robust: draw inside of cube

        skyboxShader.activate();
        glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(viewNoTranslation)); // uView loc=0
        glUniformMatrix4fv(1, 1, GL_FALSE, glm::value_ptr(proj));             // uProj loc=1

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);
        GLint skyLoc = glGetUniformLocation(skyboxShader.get(), "uSkybox");
        glUniform1i(skyLoc, 0);

        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        // Restore state
        if (cullWasEnabled) glEnable(GL_CULL_FACE);
        glDepthMask(oldDepthMask);
        glDepthFunc(oldDepthFunc);

        // Bind skybox texture for ocean shader
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);  

        // --- Render ocean ---
        renderOcean(window);

        glfwPollEvents();
        handleKeyboardInput(window);
        glfwSwapBuffers(window);
    }
}

void handleKeyboardInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GL_TRUE);
    }
}