#pragma once
#include <GLFW/glfw3.h>
#include <utilities/window.hpp>
#include <glm/mat4x4.hpp>

const glm::mat4& getOceanView();
const glm::mat4& getOceanProj();

// Keep the same function names so you do not have to touch runProgram yet.
void initOcean(GLFWwindow* window, CommandLineOptions options);
void updateOcean(GLFWwindow* window);
void renderOcean(GLFWwindow* window);