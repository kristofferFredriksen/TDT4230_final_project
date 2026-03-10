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

static const float screenQuadVertices[] = {
    // pos      // uv
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f
};

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

static GLuint createColorTexture(int width, int height)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

static GLuint createFramebufferWithColor(GLuint texture, bool withDepth)
{
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    GLuint depthRbo = 0;
    if (withDepth) {
        glGenRenderbuffers(1, &depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        int width = 0;
        int height = 0;
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        glBindTexture(GL_TEXTURE_2D, 0);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer setup failed.\n";
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

void runProgram(GLFWwindow* window, CommandLineOptions options)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screenQuadVertices), screenQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

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
    Gloom::Shader bloomExtractShader;
    bloomExtractShader.makeBasicShader("../res/shaders/post.vert", "../res/shaders/bloom_extract.frag");
    Gloom::Shader bloomBlurShader;
    bloomBlurShader.makeBasicShader("../res/shaders/post.vert", "../res/shaders/bloom_blur.frag");
    Gloom::Shader bloomCompositeShader;
    bloomCompositeShader.makeBasicShader("../res/shaders/post.vert", "../res/shaders/bloom_composite.frag");

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

    int cachedWidth = 0;
    int cachedHeight = 0;
    GLuint hdrSceneTex = 0;
    GLuint brightTex = 0;
    GLuint blurTex[2] = { 0, 0 };
    GLuint hdrFbo = 0;
    GLuint brightFbo = 0;
    GLuint blurFbo[2] = { 0, 0 };

    auto rebuildPostProcessTargets = [&](int width, int height) {
        if (hdrFbo != 0) {
            glDeleteFramebuffers(1, &hdrFbo);
            glDeleteFramebuffers(1, &brightFbo);
            glDeleteFramebuffers(2, blurFbo);
            glDeleteTextures(1, &hdrSceneTex);
            glDeleteTextures(1, &brightTex);
            glDeleteTextures(2, blurTex);
        }

        cachedWidth = width;
        cachedHeight = height;

        hdrSceneTex = createColorTexture(width, height);
        brightTex = createColorTexture(width, height);
        blurTex[0] = createColorTexture(width, height);
        blurTex[1] = createColorTexture(width, height);

        hdrFbo = createFramebufferWithColor(hdrSceneTex, true);
        brightFbo = createFramebufferWithColor(brightTex, false);
        blurFbo[0] = createFramebufferWithColor(blurTex[0], false);
        blurFbo[1] = createFramebufferWithColor(blurTex[1], false);
    };

    // Ocean init
    initOcean(window, options);

    while (!glfwWindowShouldClose(window))
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth != cachedWidth || framebufferHeight != cachedHeight) {
            rebuildPostProcessTargets(framebufferWidth, framebufferHeight);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, hdrFbo);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
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

        // --- Bright pass ---
        glBindFramebuffer(GL_FRAMEBUFFER, brightFbo);
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        bloomExtractShader.activate();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrSceneTex);
        glUniform1i(glGetUniformLocation(bloomExtractShader.get(), "uScene"), 0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // --- Blur ---
        bloomBlurShader.activate();
        bool horizontal = true;
        GLuint sourceTex = brightTex;
        for (int i = 0; i < 8; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, blurFbo[horizontal ? 0 : 1]);
            glClear(GL_COLOR_BUFFER_BIT);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, sourceTex);
            glUniform1i(glGetUniformLocation(bloomBlurShader.get(), "uImage"), 0);
            glUniform2f(glGetUniformLocation(bloomBlurShader.get(), "uDirection"), horizontal ? 1.0f : 0.0f, horizontal ? 0.0f : 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            sourceTex = blurTex[horizontal ? 0 : 1];
            horizontal = !horizontal;
        }

        // --- Composite ---
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        bloomCompositeShader.activate();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrSceneTex);
        glUniform1i(glGetUniformLocation(bloomCompositeShader.get(), "uScene"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sourceTex);
        glUniform1i(glGetUniformLocation(bloomCompositeShader.get(), "uBloom"), 1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
        glBindVertexArray(0);

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
