// src/main.cpp
// 第 7 天：球棍模型——多个原子球 + 圆柱键，画出甲烷 CH4。

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "shader.h"
#include "sphere.h"
#include "cylinder.h"
#include "molecule.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>   // std::max
#include <iostream>
#include <cmath>
#include <vector>

const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// ==================== 相机全局状态 ====================
float g_yaw    = 0.6f;      // 给个初始角度，一打开就能看到立体感
float g_pitch  = 0.4f;
float g_radius = 5.0f;
float g_lastX  = SCR_WIDTH  / 2.0f;
float g_lastY  = SCR_HEIGHT / 2.0f;
bool  g_firstMouse = true;

const float SENSITIVITY = 0.005f;
const float ZOOM_STEP   = 0.3f;
const float PITCH_LIMIT = 1.5533f;
const float RADIUS_MIN  = 1.0f;
const float RADIUS_MAX  = 30.0f;

glm::vec3 g_cameraPos(0.0f, 0.0f, 5.0f);

// ==================== 回调函数 ====================
static void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height)
{
    glViewport(0, 0, width, height);
}

static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (g_firstMouse)
    {
        g_lastX = (float)xpos;
        g_lastY = (float)ypos;
        g_firstMouse = false;
        return;
    }

    float xoffset = (float)xpos - g_lastX;
    float yoffset = g_lastY - (float)ypos;
    g_lastX = (float)xpos;
    g_lastY = (float)ypos;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
        return;

    xoffset *= SENSITIVITY;
    yoffset *= SENSITIVITY;

    g_yaw   += xoffset;
    g_pitch += yoffset;

    if (g_pitch >  PITCH_LIMIT) g_pitch =  PITCH_LIMIT;
    if (g_pitch < -PITCH_LIMIT) g_pitch = -PITCH_LIMIT;
}

static void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    g_radius -= (float)yoffset * ZOOM_STEP;
    if (g_radius < RADIUS_MIN) g_radius = RADIUS_MIN;
    if (g_radius > RADIUS_MAX) g_radius = RADIUS_MAX;
}

static void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// ==================== 工具函数 ====================

// 根据两个原子位置，构造一个把"沿 Y 轴的单位圆柱"摆到两点之间的 Model 矩阵
static glm::mat4 makeBondModel(const glm::vec3& a, const glm::vec3& b)
{
    glm::vec3 mid  = (a + b) * 0.5f;      // 中点：圆柱中心放这里
    glm::vec3 dir  = b - a;               // 从 a 指向 b
    float     len  = glm::length(dir);    // 键长

    glm::mat4 model = glm::mat4(1.0f);

    // 1. 平移到中点
    model = glm::translate(model, mid);

    // 2. 旋转：把默认的 +Y 方向转到 dir 方向
    if (len > 1e-6f)
    {
        glm::vec3 from = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 to   = glm::normalize(dir);

        // clamp 防止浮点误差导致 acos 的输入超出 [-1, 1] 而返回 NaN
        float cosAngle = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
        float angle    = std::acos(cosAngle);
        glm::vec3 axis = glm::cross(from, to);

        // 处理"平行"的退化情况：叉积为零向量时无法确定旋转轴
        if (glm::length(axis) < 1e-6f)
        {
            if (cosAngle > 0.0f)
            {
                // 同向：不需要旋转（angle 已经是 0）
            }
            else
            {
                // 反向：需要转 180°，任取一个垂直于 Y 的轴（用 X 轴）
                model = glm::rotate(model, 3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
            }
        }
        else
        {
            model = glm::rotate(model, angle, glm::normalize(axis));
        }
    }

    // 3. 沿 Y 轴缩放到键长（圆柱原始高度是 1）
    model = glm::scale(model, glm::vec3(1.0f, len, 1.0f));

    return model;
}

int main()
{
    // ==================== 初始化 ====================
    if (!glfwInit())
    {
        std::cerr << "[错误] GLFW 初始化失败！" << std::endl;
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                                          "Molecule Viewer - Day 7", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建窗口失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "[错误] GLAD 初始化失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "OpenGL: " << glGetString(GL_VERSION)
              << " | " << glGetString(GL_RENDERER) << std::endl;

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ==================== 生成网格 ====================
    // 单位球（半径 1），靠 Model 缩放成不同大小的原子
    std::vector<float> sphereVerts;
    std::vector<unsigned int> sphereIdx;
    generateSphere(1.0f, 32, 16, sphereVerts, sphereIdx);

    // 单位圆柱（半径 1、高度 1），靠 Model 缩放成不同粗细、长度的键
    std::vector<float> cylVerts;
    std::vector<unsigned int> cylIdx;
    generateCylinder(1.0f, 20, cylVerts, cylIdx);

    std::cout << "球体三角形: " << sphereIdx.size() / 3
              << "，圆柱三角形: " << cylIdx.size() / 3 << std::endl;

    // ---------- 设置球体的 VAO ----------
    unsigned int sphereVAO, sphereVBO, sphereEBO;
    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    glBindVertexArray(sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sphereVerts.size() * sizeof(float),
                 sphereVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphereIdx.size() * sizeof(unsigned int),
                 sphereIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ---------- 设置圆柱的 VAO ----------
    unsigned int cylVAO, cylVBO, cylEBO;
    glGenVertexArrays(1, &cylVAO);
    glGenBuffers(1, &cylVBO);
    glGenBuffers(1, &cylEBO);

    glBindVertexArray(cylVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cylVBO);
    glBufferData(GL_ARRAY_BUFFER, cylVerts.size() * sizeof(float),
                 cylVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cylEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cylIdx.size() * sizeof(unsigned int),
                 cylIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ==================== 着色器 ====================
    Shader sphereShader("src/shaders/sphere.vert", "src/shaders/sphere.frag");
    Shader cylinderShader("src/shaders/cylinder.vert", "src/shaders/cylinder.frag");

    // ==================== 分子数据 ====================
    Molecule methane = makeMethane();
    std::cout << "甲烷：原子数 " << methane.atoms.size()
              << "，键数 " << methane.bonds.size() << std::endl;

    // 打印分子最大半径（后面根据它决定相机距离）
    float maxDist = 0.0f;
    for (const auto& atom : methane.atoms)
        maxDist = std::max(maxDist, glm::length(atom.position));
    std::cout << "分子最大半径: " << maxDist << std::endl;

    // ==================== 渲染循环 ====================
    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        glClearColor(0.12f, 0.12f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---------- 相机 ----------
        float camX = g_radius * std::cos(g_pitch) * std::sin(g_yaw);
        float camY = g_radius * std::sin(g_pitch);
        float camZ = g_radius * std::cos(g_pitch) * std::cos(g_yaw);
        g_cameraPos = glm::vec3(camX, camY, camZ);

        glm::mat4 view = glm::lookAt(g_cameraPos,
                                     glm::vec3(0.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT,
            0.1f,
            100.0f);

        // 光源位置：相机右上方（跟随相机，保证总有亮面）
        glm::vec3 lightPos(g_cameraPos.x + 2.0f,
                           g_cameraPos.y + 2.0f,
                           g_cameraPos.z);

        // ==================== 第 1 步：画所有原子球 ====================
        sphereShader.use();

        // 这些 uniform 对所有原子都一样，循环外传一次
        glUniformMatrix4fv(glGetUniformLocation(sphereShader.ID, "view"),
                           1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(sphereShader.ID, "projection"),
                           1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(sphereShader.ID, "lightColor"), 1.0f, 1.0f, 1.0f);
        glUniform3f(glGetUniformLocation(sphereShader.ID, "lightPos"),
                    lightPos.x, lightPos.y, lightPos.z);
        glUniform3f(glGetUniformLocation(sphereShader.ID, "viewPos"),
                    g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);

        glBindVertexArray(sphereVAO);

        for (const auto& atom : methane.atoms)
        {
            ElementStyle style = getElementStyle(atom.element);

            // 每个原子一个 Model 矩阵：移到原子位置 + 缩放到对应半径
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, atom.position);
            model = glm::scale(model, glm::vec3(style.radius));

            glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(model)));

            glUniformMatrix4fv(glGetUniformLocation(sphereShader.ID, "model"),
                               1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(glGetUniformLocation(sphereShader.ID, "normalMatrix"),
                               1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniform3f(glGetUniformLocation(sphereShader.ID, "objectColor"),
                        style.color.r, style.color.g, style.color.b);

            glDrawElements(GL_TRIANGLES, (GLsizei)sphereIdx.size(),
                           GL_UNSIGNED_INT, 0);
        }

        // ==================== 第 2 步：画所有化学键 ====================
        cylinderShader.use();

        glUniformMatrix4fv(glGetUniformLocation(cylinderShader.ID, "view"),
                           1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(cylinderShader.ID, "projection"),
                           1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(glGetUniformLocation(cylinderShader.ID, "lightColor"),
                    1.0f, 1.0f, 1.0f);
        glUniform3f(glGetUniformLocation(cylinderShader.ID, "lightPos"),
                    lightPos.x, lightPos.y, lightPos.z);
        glUniform3f(glGetUniformLocation(cylinderShader.ID, "viewPos"),
                    g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);

        // 键的颜色：浅灰色
        glUniform3f(glGetUniformLocation(cylinderShader.ID, "objectColor"),
                    0.65f, 0.65f, 0.68f);

        glBindVertexArray(cylVAO);

        // 键半径（把半径 1 的圆柱缩到 0.07）
        const float BOND_RADIUS = 0.07f;

        for (const auto& bond : methane.bonds)
        {
            glm::vec3 a = methane.atoms[bond.a].position;
            glm::vec3 b = methane.atoms[bond.b].position;

            glm::mat4 model = makeBondModel(a, b);
            // 再把半径方向（X/Z）缩到指定粗细
            model = glm::scale(model, glm::vec3(BOND_RADIUS, 1.0f, BOND_RADIUS));

            glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(model)));

            glUniformMatrix4fv(glGetUniformLocation(cylinderShader.ID, "model"),
                               1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(glGetUniformLocation(cylinderShader.ID, "normalMatrix"),
                               1, GL_FALSE, glm::value_ptr(normalMatrix));

            glDrawElements(GL_TRIANGLES, (GLsizei)cylIdx.size(),
                           GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ==================== 清理 ====================
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &cylVAO);
    glDeleteBuffers(1, &cylVBO);
    glDeleteBuffers(1, &cylEBO);

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
