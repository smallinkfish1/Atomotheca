// src/main.cpp
// 第 9 天：CPK 元素配色 + 完整 Blinn-Phong 光照（含镜面高光与补光）。
// 修复：第 8 天空间填充模型暗部"死黑"的问题——通过提高环境光 + 增加补光解决。

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "shader.h"
#include "sphere.h"
#include "cylinder.h"
#include "molecule.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <iostream>
#include <cmath>
#include <vector>

const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// ==================== 渲染模式 ====================
enum class RenderMode
{
    BallAndStick,    // 球棍模型
    SpaceFilling     // 空间填充模型
};

RenderMode g_mode = RenderMode::BallAndStick;

// 空间填充模型下，把范德华半径乘一个系数
// 略小于 1 可以减少球之间的完全重合，缓解 Z-fighting
const float VDW_SCALE = 0.98f;

// ==================== 光照参数 ====================
// 主光：跟着相机走，位于右上方（保证任何时候都有亮面）
const glm::vec3 KEY_LIGHT_OFFSET  = glm::vec3( 2.0f,  2.0f, 0.0f);
// 补光：位于左下方，强度低、色调略冷，专门填主光的死角
// 这是修复"暗部死黑"的关键
const glm::vec3 FILL_LIGHT_OFFSET = glm::vec3(-2.5f, -1.5f, 0.0f);
const glm::vec3 FILL_LIGHT_COLOR  = glm::vec3( 0.38f,  0.38f, 0.45f);

// 化学键的颜色（中性浅灰，不抢原子的视觉）
const glm::vec3 BOND_COLOR = glm::vec3(0.62f, 0.62f, 0.66f);
const float     BOND_RADIUS = 0.07f;

// ==================== 相机全局状态 ====================
float g_yaw    = 0.6f;
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

// ==================== 键盘回调（切换模式） ====================
static void key_callback(GLFWwindow* /*window*/, int key, int /*scancode*/,
                         int action, int /*mods*/)
{
    // 只在"按下"的瞬间触发，避免长按重复切换
    if (action != GLFW_PRESS)
        return;

    if (key == GLFW_KEY_SPACE)
    {
        g_mode = (g_mode == RenderMode::BallAndStick)
                     ? RenderMode::SpaceFilling
                     : RenderMode::BallAndStick;

        std::cout << "切换模式: "
                  << (g_mode == RenderMode::BallAndStick ? "球棍模型" : "空间填充模型")
                  << std::endl;
    }
}

// ==================== 其他回调 ====================
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

// ==================== 键的 Model 矩阵（第 7 天） ====================
static glm::mat4 makeBondModel(const glm::vec3& a, const glm::vec3& b)
{
    glm::vec3 mid = (a + b) * 0.5f;
    glm::vec3 dir = b - a;
    float     len = glm::length(dir);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, mid);

    if (len > 1e-6f)
    {
        glm::vec3 from = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 to   = glm::normalize(dir);

        float cosAngle = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
        float angle    = std::acos(cosAngle);
        glm::vec3 axis = glm::cross(from, to);

        if (glm::length(axis) < 1e-6f)
        {
            if (cosAngle <= 0.0f)
                model = glm::rotate(model, 3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        else
        {
            model = glm::rotate(model, angle, glm::normalize(axis));
        }
    }

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

    // 请求 24 位深度缓冲，提高深度精度（缓解 Z-fighting）
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                                          "Molecule Viewer - Day 9", nullptr, nullptr);
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
    glfwSetKeyCallback(window, key_callback);

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
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ==================== 网格 ====================
    std::vector<float> sphereVerts;
    std::vector<unsigned int> sphereIdx;
    generateSphere(1.0f, 32, 16, sphereVerts, sphereIdx);

    std::vector<float> cylVerts;
    std::vector<unsigned int> cylIdx;
    generateCylinder(1.0f, 20, cylVerts, cylIdx);

    std::cout << "球体三角形: " << sphereIdx.size() / 3
              << "，圆柱三角形: " << cylIdx.size() / 3 << std::endl;

    // ---------- 球体 VAO ----------
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

    // ---------- 圆柱 VAO ----------
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

    // ---- sphere 的 uniform 位置 ----
    GLint s_model      = glGetUniformLocation(sphereShader.ID, "model");
    GLint s_view       = glGetUniformLocation(sphereShader.ID, "view");
    GLint s_proj       = glGetUniformLocation(sphereShader.ID, "projection");
    GLint s_normalMat  = glGetUniformLocation(sphereShader.ID, "normalMatrix");
    GLint s_objColor   = glGetUniformLocation(sphereShader.ID, "objectColor");
    GLint s_lightColor = glGetUniformLocation(sphereShader.ID, "lightColor");
    GLint s_lightPos   = glGetUniformLocation(sphereShader.ID, "lightPos");
    GLint s_fillPos    = glGetUniformLocation(sphereShader.ID, "fillLightPos");
    GLint s_fillColor  = glGetUniformLocation(sphereShader.ID, "fillLightColor");
    GLint s_viewPos    = glGetUniformLocation(sphereShader.ID, "viewPos");
    GLint s_normalOff  = glGetUniformLocation(sphereShader.ID, "normalOffset");

    // ---- cylinder 的 uniform 位置 ----
    GLint c_model      = glGetUniformLocation(cylinderShader.ID, "model");
    GLint c_view       = glGetUniformLocation(cylinderShader.ID, "view");
    GLint c_proj       = glGetUniformLocation(cylinderShader.ID, "projection");
    GLint c_normalMat  = glGetUniformLocation(cylinderShader.ID, "normalMatrix");
    GLint c_objColor   = glGetUniformLocation(cylinderShader.ID, "objectColor");
    GLint c_lightColor = glGetUniformLocation(cylinderShader.ID, "lightColor");
    GLint c_lightPos   = glGetUniformLocation(cylinderShader.ID, "lightPos");
    GLint c_fillPos    = glGetUniformLocation(cylinderShader.ID, "fillLightPos");
    GLint c_fillColor  = glGetUniformLocation(cylinderShader.ID, "fillLightColor");
    GLint c_viewPos    = glGetUniformLocation(cylinderShader.ID, "viewPos");

    // ==================== 分子 ====================
    Molecule methane = makeMethane();
    std::cout << "甲烷：原子数 " << methane.atoms.size()
              << "，键数 " << methane.bonds.size() << std::endl;
    std::cout << "按 [空格] 切换 球棍模型 / 空间填充模型" << std::endl;

    // 排序用：存 (到相机的距离, 原子索引)
    std::vector<std::pair<float, int>> drawOrder;

    // ==================== 渲染循环 ====================
    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        // 背景色：配深灰碳球一起压暗，保证碳球的最暗处（≈0.126）仍然亮于背景
        glClearColor(0.08f, 0.08f, 0.13f, 1.0f);
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

        // ---------- 两盏灯的位置 ----------
        glm::vec3 lightPos     = g_cameraPos + KEY_LIGHT_OFFSET;   // 主光
        glm::vec3 fillLightPos = g_cameraPos + FILL_LIGHT_OFFSET;  // 补光

        // ==================== 排序（远 → 近）====================
        drawOrder.clear();
        for (int i = 0; i < (int)methane.atoms.size(); ++i)
        {
            float d = glm::length(methane.atoms[i].position - g_cameraPos);
            drawOrder.push_back({ d, i });
        }
        std::sort(drawOrder.begin(), drawOrder.end(),
                  [](const std::pair<float, int>& a,
                     const std::pair<float, int>& b)
                  {
                      return a.first > b.first;
                  });

        // ==================== 画原子 ====================
        sphereShader.use();

        glUniformMatrix4fv(s_view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(s_proj, 1, GL_FALSE, glm::value_ptr(projection));

        // 主光
        glUniform3f(s_lightColor, 1.0f, 1.0f, 1.0f);
        glUniform3f(s_lightPos, lightPos.x, lightPos.y, lightPos.z);
        // 补光
        glUniform3f(s_fillColor, FILL_LIGHT_COLOR.r, FILL_LIGHT_COLOR.g, FILL_LIGHT_COLOR.b);
        glUniform3f(s_fillPos, fillLightPos.x, fillLightPos.y, fillLightPos.z);
        // 相机位置（算高光用）
        glUniform3f(s_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);

        glUniform1f(s_normalOff, (g_mode == RenderMode::SpaceFilling) ? -0.002f : 0.0f);

        glBindVertexArray(sphereVAO);

        for (const auto& pair : drawOrder)
        {
            const Atom& atom = methane.atoms[pair.second];
            ElementStyle style = getElementStyle(atom.element);

            float r = (g_mode == RenderMode::BallAndStick)
                          ? style.ballRadius
                          : style.vdwRadius * VDW_SCALE;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, atom.position);
            model = glm::scale(model, glm::vec3(r));

            glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(model)));

            glUniformMatrix4fv(s_model, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(s_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniform3f(s_objColor, style.color.r, style.color.g, style.color.b);

            glDrawElements(GL_TRIANGLES, (GLsizei)sphereIdx.size(),
                           GL_UNSIGNED_INT, 0);
        }

        // ==================== 画化学键（只在球棍模式）====================
        if (g_mode == RenderMode::BallAndStick)
        {
            cylinderShader.use();

            glUniformMatrix4fv(c_view, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(c_proj, 1, GL_FALSE, glm::value_ptr(projection));

            glUniform3f(c_lightColor, 1.0f, 1.0f, 1.0f);
            glUniform3f(c_lightPos, lightPos.x, lightPos.y, lightPos.z);
            glUniform3f(c_fillColor, FILL_LIGHT_COLOR.r, FILL_LIGHT_COLOR.g, FILL_LIGHT_COLOR.b);
            glUniform3f(c_fillPos, fillLightPos.x, fillLightPos.y, fillLightPos.z);
            glUniform3f(c_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);

            glUniform3f(c_objColor, BOND_COLOR.r, BOND_COLOR.g, BOND_COLOR.b);

            glBindVertexArray(cylVAO);

            for (const auto& bond : methane.bonds)
            {
                glm::vec3 a = methane.atoms[bond.a].position;
                glm::vec3 b = methane.atoms[bond.b].position;

                glm::mat4 model = makeBondModel(a, b);
                model = glm::scale(model, glm::vec3(BOND_RADIUS, 1.0f, BOND_RADIUS));

                glm::mat3 normalMatrix =
                    glm::transpose(glm::inverse(glm::mat3(model)));

                glUniformMatrix4fv(c_model, 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix3fv(c_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));

                glDrawElements(GL_TRIANGLES, (GLsizei)cylIdx.size(),
                               GL_UNSIGNED_INT, 0);
            }
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
