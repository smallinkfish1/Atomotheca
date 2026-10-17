// src/main.cpp
// 第 11 天：Dear ImGui 控制面板。
//   - 显示模式切换（球棍 / 空间填充）、原子半径缩放
//   - 自动旋转开关与速度
//   - 光照参数实时调节（环境光 / 高光 / 光泽度 / 补光）
//   - 背景色
//   - 元素图例（CPK 配色 + 半径）
//
// 注意：UI 标签用英文。原因见包内《使用说明.md》——
//       ImGui 自带的字体不含中文字形，要用中文必须额外加载中文字体文件。

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "shader.h"
#include "sphere.h"
#include "cylinder.h"
#include "molecule.h"
#include "molfile.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// 窗口加宽，给左侧控制面板留出位置
const unsigned int SCR_WIDTH  = 1100;
const unsigned int SCR_HEIGHT = 700;

// ==================== UI 可调状态 ====================
// 把所有"用户能调"的参数集中在一个结构体里，
// 比散落一堆全局变量清楚得多。
struct UiState
{
    // 显示
    int   renderMode = 0;          // 0 = 球棍模型, 1 = 空间填充模型
    float ballScale  = 1.0f;       // 球棍模型的原子半径缩放系数
    float vdwScale   = 0.98f;      // 空间填充模型的半径缩放系数

    // 动画
    bool  autoRotate  = false;
    float rotateSpeed = 0.6f;      // 弧度 / 秒

    // 光照
    float ambientStrength  = 0.35f;
    float specularStrength = 0.55f;
    float shininess        = 64.0f;
    float fillStrength     = 1.0f; // 补光强度倍率

    // 背景
    float bgColor[3] = { 0.08f, 0.08f, 0.13f };

    // 面板
    bool showPanel = true;
};

UiState g_ui;

// 自动旋转的累计角度
float g_autoAngle = 0.0f;

// 上一帧时间（用于计算 deltaTime）
float g_lastFrame = 0.0f;

// ==================== 光照常量 ====================
const glm::vec3 KEY_LIGHT_OFFSET  = glm::vec3( 2.0f,  2.0f, 0.0f);   // 主光：相机右上方
const glm::vec3 FILL_LIGHT_OFFSET = glm::vec3(-2.5f, -1.5f, 0.0f);   // 补光：相机左下方
const glm::vec3 FILL_LIGHT_BASE   = glm::vec3( 0.38f,  0.38f, 0.45f); // 补光基准色（略偏冷）

// 化学键外观
const glm::vec3 BOND_COLOR  = glm::vec3(0.62f, 0.62f, 0.66f);
const float     BOND_RADIUS = 0.07f;

// ==================== 相机全局状态 ====================
float g_yaw    = 0.6f;
float g_pitch  = 0.4f;
float g_radius = 5.0f;
float g_lastX  = SCR_WIDTH  / 2.0f;
float g_lastY  = SCR_HEIGHT / 2.0f;
bool  g_firstMouse = true;

const float SENSITIVITY = 0.005f;
const float PITCH_LIMIT = 1.5533f;

float g_radiusMin = 1.0f;
float g_radiusMax = 30.0f;

glm::vec3 g_cameraPos(0.0f, 0.0f, 5.0f);

// ==================== ImGui 状态查询辅助 ====================
// 回调可能在 ImGui 初始化之前被触发，所以要判空，避免崩溃。

static bool imguiWantsMouse()
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    return ctx != nullptr && ImGui::GetIO().WantCaptureMouse;
}

static bool imguiWantsKeyboard()
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    return ctx != nullptr && ImGui::GetIO().WantCaptureKeyboard;
}

// ==================== 键盘回调 ====================
static void key_callback(GLFWwindow* /*window*/, int key, int /*scancode*/,
                         int action, int /*mods*/)
{
    if (action != GLFW_PRESS)          // 过滤长按重复
        return;
    if (imguiWantsKeyboard())          // 正在 UI 里输入时不响应
        return;

    if (key == GLFW_KEY_SPACE)
    {
        g_ui.renderMode = (g_ui.renderMode == 0) ? 1 : 0;
        std::cout << "显示模式: "
                  << (g_ui.renderMode == 0 ? "球棍模型" : "空间填充模型")
                  << std::endl;
    }
    else if (key == GLFW_KEY_TAB)
    {
        g_ui.showPanel = !g_ui.showPanel;
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

    // 鼠标在 ImGui 面板上时不要旋转视角
    // （注意 lastX/lastY 已经更新过了，所以移出面板后不会"跳一下"）
    if (imguiWantsMouse())
        return;

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
    if (imguiWantsMouse())
        return;

    float factor = 1.0f - (float)yoffset * 0.12f;
    g_radius *= factor;

    if (g_radius < g_radiusMin) g_radius = g_radiusMin;
    if (g_radius > g_radiusMax) g_radius = g_radiusMax;
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

// ==================== 控制面板 ====================
static void DrawControlPanel(const std::string& fileName,
                             const Molecule& molecule,
                             const std::vector<std::string>& elements,
                             float molR)
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f),  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90f);

    if (ImGui::Begin("Control Panel", &g_ui.showPanel))
    {
        // ---------- 分子信息 ----------
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "MOLECULE");
        ImGui::Separator();
        ImGui::Text("File   %s", fileName.c_str());
        ImGui::Text("Atoms  %d", static_cast<int>(molecule.atoms.size()));
        ImGui::Text("Bonds  %d", static_cast<int>(molecule.bonds.size()));
        ImGui::Text("Radius %.2f A", molR);
        ImGui::Spacing();

        // ---------- 显示模式 ----------
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "DISPLAY");
        ImGui::Separator();
        ImGui::RadioButton("Ball-and-Stick", &g_ui.renderMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Space-Filling",  &g_ui.renderMode, 1);

        if (g_ui.renderMode == 0)
            ImGui::SliderFloat("Atom radius", &g_ui.ballScale, 0.30f, 2.00f, "%.2f x");
        else
            ImGui::SliderFloat("Fill factor", &g_ui.vdwScale,  0.80f, 1.10f, "%.2f");
        ImGui::Spacing();

        // ---------- 动画 ----------
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "ANIMATION");
        ImGui::Separator();
        ImGui::Checkbox("Auto rotate", &g_ui.autoRotate);
        if (g_ui.autoRotate)
            ImGui::SliderFloat("Speed", &g_ui.rotateSpeed, 0.0f, 3.0f, "%.2f rad/s");
        ImGui::Spacing();

        // ---------- 光照 ----------
        if (ImGui::CollapsingHeader("LIGHTING", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Ambient",    &g_ui.ambientStrength,  0.00f, 1.00f,  "%.2f");
            ImGui::SliderFloat("Specular",   &g_ui.specularStrength, 0.00f, 2.00f,  "%.2f");
            ImGui::SliderFloat("Shininess",  &g_ui.shininess,        1.00f, 256.0f, "%.0f");
            ImGui::SliderFloat("Fill light", &g_ui.fillStrength,     0.00f, 2.00f,  "%.2f");

            if (ImGui::Button("Reset lighting"))
            {
                g_ui.ambientStrength  = 0.35f;
                g_ui.specularStrength = 0.55f;
                g_ui.shininess        = 64.0f;
                g_ui.fillStrength     = 1.0f;
            }
        }

        // ---------- 背景 ----------
        if (ImGui::CollapsingHeader("BACKGROUND"))
        {
            ImGui::ColorEdit3("Color", g_ui.bgColor);
            if (ImGui::Button("Reset background"))
            {
                g_ui.bgColor[0] = 0.08f;
                g_ui.bgColor[1] = 0.08f;
                g_ui.bgColor[2] = 0.13f;
            }
        }

        // ---------- 元素图例 ----------
        if (ImGui::CollapsingHeader("ELEMENT LEGEND", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const auto& el : elements)
            {
                ElementStyle st = getElementStyle(el);

                ImVec4 col(st.color.r, st.color.g, st.color.b, 1.0f);
                ImGui::ColorButton(("##swatch_" + el).c_str(),
                                   col,
                                   ImGuiColorEditFlags_NoTooltip |
                                   ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(20.0f, 20.0f));
                ImGui::SameLine();
                ImGui::Text("%-3s  ball %.2f   vdW %.2f",
                            el.c_str(), st.ballRadius, st.vdwRadius);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Drag LMB : rotate view");
        ImGui::TextDisabled("Scroll   : zoom");
        ImGui::TextDisabled("Space    : switch model");
        ImGui::TextDisabled("Tab      : show / hide panel");
        ImGui::TextDisabled("Esc      : quit");
    }
    ImGui::End();
}

int main(int argc, char** argv)
{
    // ==================== 0. 决定加载哪个文件 ====================
    std::string molPath = (argc > 1) ? argv[1] : "assets/methane.mol";

    // 只显示文件名（用于面板标题）
    std::string fileName = molPath;
    {
        size_t slash = fileName.find_last_of("/\\");
        if (slash != std::string::npos)
            fileName = fileName.substr(slash + 1);
    }

    // ==================== 1. 初始化 GLFW ====================
    if (!glfwInit())
    {
        std::cerr << "[错误] GLFW 初始化失败！" << std::endl;
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                                          "Molecule Viewer - Day 11", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建窗口失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);   // 开启垂直同步，避免无谓的满负荷渲染

    // ★ 先注册我们自己的回调，再初始化 ImGui
    //   因为 ImGui 安装回调时会"链式保存"当前的回调，
    //   之后的调用顺序是：ImGui 的处理 → 我们的处理
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

    // ==================== 2. 初始化 Dear ImGui ====================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;      // 不生成 imgui.ini，保持工作目录干净

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);   // true = 安装 ImGui 的回调
    ImGui_ImplOpenGL3_Init("#version 330");

    // ==================== 3. 加载分子 ====================
    Molecule molecule;
    if (!molfile::loadMolecule(molPath, molecule))
    {
        std::cerr << "[错误] 加载分子文件失败，程序退出。" << std::endl;
        std::cerr << "       用法: ./build/molecule_viewer [分子文件路径]" << std::endl;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "已加载: " << molPath << std::endl;
    std::cout << "  原子数: " << molecule.atoms.size()
              << "，键数: " << molecule.bonds.size() << std::endl;

    // 统计元素组成（顺便给 UI 的图例用）
    std::vector<std::string> elements;
    {
        std::map<std::string, int> counts;
        for (const auto& a : molecule.atoms)
            counts[a.element]++;

        std::cout << "  元素组成: ";
        for (const auto& kv : counts)
        {
            std::cout << kv.first << kv.second << " ";
            elements.push_back(kv.first);   // map 已按字母序排好
        }
        std::cout << std::endl;
    }

    // ==================== 4. 自动相机 ====================
    float molR = molfile::moleculeRadius(molecule);
    if (molR < 0.1f) molR = 1.0f;

    const float FOV_DEG = 45.0f;
    float dist = molR / std::tan(glm::radians(FOV_DEG) * 0.5f) * 1.55f;

    g_radiusMin = dist * 0.12f;
    g_radiusMax = dist * 5.0f;
    g_radius    = dist;
    g_lastFrame = (float)glfwGetTime();

    std::cout << "  分子半径: " << molR
              << "，相机初始距离: " << dist << std::endl;
    std::cout << "操作: 左键拖拽旋转 | 滚轮缩放 | 空格切换模型 | Tab 隐藏面板 | Esc 退出"
              << std::endl;

    // ==================== 5. 网格 ====================
    std::vector<float> sphereVerts;
    std::vector<unsigned int> sphereIdx;
    generateSphere(1.0f, 32, 16, sphereVerts, sphereIdx);

    std::vector<float> cylVerts;
    std::vector<unsigned int> cylIdx;
    generateCylinder(1.0f, 20, cylVerts, cylIdx);

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

    // ==================== 6. 着色器 ====================
    Shader sphereShader("src/shaders/sphere.vert", "src/shaders/sphere.frag");
    Shader cylinderShader("src/shaders/cylinder.vert", "src/shaders/cylinder.frag");

    // ---- sphere uniforms ----
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
    GLint s_ambient    = glGetUniformLocation(sphereShader.ID, "ambientStrength");
    GLint s_specular   = glGetUniformLocation(sphereShader.ID, "specularStrength");
    GLint s_shiny      = glGetUniformLocation(sphereShader.ID, "shininess");

    // ---- cylinder uniforms ----
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
    GLint c_ambient    = glGetUniformLocation(cylinderShader.ID, "ambientStrength");
    GLint c_specular   = glGetUniformLocation(cylinderShader.ID, "specularStrength");
    GLint c_shiny      = glGetUniformLocation(cylinderShader.ID, "shininess");

    // 排序用
    std::vector<std::pair<float, int>> drawOrder;

    // ==================== 7. 渲染循环 ====================
    while (!glfwWindowShouldClose(window))
    {
        // ---------- 时间步长 ----------
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - g_lastFrame;
        g_lastFrame = currentFrame;
        if (deltaTime > 0.1f) deltaTime = 0.1f;   // 防止切窗口后跳变

        // ---------- 事件（必须在 ImGui NewFrame 之前）----------
        glfwPollEvents();
        processInput(window);

        // ---------- 自动旋转 ----------
        if (g_ui.autoRotate)
            g_autoAngle += g_ui.rotateSpeed * deltaTime;

        // ---------- ImGui 新帧 ----------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (g_ui.showPanel)
            DrawControlPanel(fileName, molecule, elements, molR);

        ImGui::Render();

        // ---------- 场景渲染 ----------
        glClearColor(g_ui.bgColor[0], g_ui.bgColor[1], g_ui.bgColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 相机
        float camX = g_radius * std::cos(g_pitch) * std::sin(g_yaw);
        float camY = g_radius * std::sin(g_pitch);
        float camZ = g_radius * std::cos(g_pitch) * std::cos(g_yaw);
        g_cameraPos = glm::vec3(camX, camY, camZ);

        glm::mat4 view = glm::lookAt(g_cameraPos,
                                     glm::vec3(0.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 projection = glm::perspective(
            glm::radians(FOV_DEG),
            (float)SCR_WIDTH / (float)SCR_HEIGHT,
            0.1f,
            500.0f);

        // 光源位置（两个偏移量按分子大小缩放）
        float lightScale = std::max(1.0f, molR);
        glm::vec3 lightPos     = g_cameraPos + KEY_LIGHT_OFFSET  * lightScale;
        glm::vec3 fillLightPos = g_cameraPos + FILL_LIGHT_OFFSET * lightScale;
        glm::vec3 fillColor    = FILL_LIGHT_BASE * g_ui.fillStrength;

        // 自动旋转矩阵
        // 注意：转的是"分子"而不是"相机"。
        // 因为我们的光源跟着相机走，如果转相机，分子表面的明暗分布不变，
        // 看起来会像"分子没动、背景在动"。转分子才能看出光在表面流转。
        glm::mat4 spin = glm::mat4(1.0f);
        if (g_autoAngle != 0.0f)
            spin = glm::rotate(glm::mat4(1.0f), g_autoAngle,
                               glm::vec3(0.0f, 1.0f, 0.0f));

        // ---------- 排序（远 → 近）----------
        drawOrder.clear();
        for (int i = 0; i < (int)molecule.atoms.size(); ++i)
        {
            float d = glm::length(molecule.atoms[i].position - g_cameraPos);
            drawOrder.push_back({ d, i });
        }
        std::sort(drawOrder.begin(), drawOrder.end(),
                  [](const std::pair<float, int>& a,
                     const std::pair<float, int>& b)
                  {
                      return a.first > b.first;
                  });

        // ---------- 画原子 ----------
        sphereShader.use();

        glUniformMatrix4fv(s_view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(s_proj, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(s_lightColor, 1.0f, 1.0f, 1.0f);
        glUniform3f(s_lightPos, lightPos.x, lightPos.y, lightPos.z);
        glUniform3f(s_fillColor, fillColor.r, fillColor.g, fillColor.b);
        glUniform3f(s_fillPos, fillLightPos.x, fillLightPos.y, fillLightPos.z);
        glUniform3f(s_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);

        // ★ 这三个由 UI 实时控制
        glUniform1f(s_ambient,  g_ui.ambientStrength);
        glUniform1f(s_specular, g_ui.specularStrength);
        glUniform1f(s_shiny,    g_ui.shininess);

        glUniform1f(s_normalOff, (g_ui.renderMode == 1) ? -0.002f : 0.0f);

        glBindVertexArray(sphereVAO);

        for (const auto& pair : drawOrder)
        {
            const Atom& atom = molecule.atoms[pair.second];
            ElementStyle style = getElementStyle(atom.element);

            float r = (g_ui.renderMode == 0)
                          ? style.ballRadius * g_ui.ballScale
                          : style.vdwRadius * g_ui.vdwScale;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, atom.position);
            model = glm::scale(model, glm::vec3(r));
            model = spin * model;

            glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(model)));

            glUniformMatrix4fv(s_model, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(s_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniform3f(s_objColor, style.color.r, style.color.g, style.color.b);

            glDrawElements(GL_TRIANGLES, (GLsizei)sphereIdx.size(),
                           GL_UNSIGNED_INT, 0);
        }

        // ---------- 画化学键（只在球棍模式）----------
        if (g_ui.renderMode == 0)
        {
            cylinderShader.use();

            glUniformMatrix4fv(c_view, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(c_proj, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3f(c_lightColor, 1.0f, 1.0f, 1.0f);
            glUniform3f(c_lightPos, lightPos.x, lightPos.y, lightPos.z);
            glUniform3f(c_fillColor, fillColor.r, fillColor.g, fillColor.b);
            glUniform3f(c_fillPos, fillLightPos.x, fillLightPos.y, fillLightPos.z);
            glUniform3f(c_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);
            glUniform1f(c_ambient,  g_ui.ambientStrength);
            glUniform1f(c_specular, g_ui.specularStrength);
            glUniform1f(c_shiny,    g_ui.shininess);
            glUniform3f(c_objColor, BOND_COLOR.r, BOND_COLOR.g, BOND_COLOR.b);

            glBindVertexArray(cylVAO);

            for (const auto& bond : molecule.bonds)
            {
                glm::vec3 a = molecule.atoms[bond.a].position;
                glm::vec3 b = molecule.atoms[bond.b].position;

                glm::mat4 model = makeBondModel(a, b);
                model = glm::scale(model, glm::vec3(BOND_RADIUS, 1.0f, BOND_RADIUS));
                model = spin * model;

                glm::mat3 normalMatrix =
                    glm::transpose(glm::inverse(glm::mat3(model)));

                glUniformMatrix4fv(c_model, 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix3fv(c_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));

                glDrawElements(GL_TRIANGLES, (GLsizei)cylIdx.size(),
                               GL_UNSIGNED_INT, 0);
            }
        }

        // ---------- ImGui 绘制（必须最后，且在交换缓冲之前）----------
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ==================== 8. 清理 ====================
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &cylVAO);
    glDeleteBuffers(1, &cylVBO);
    glDeleteBuffers(1, &cylEBO);

    // ImGui 要先于 GLFW 清理
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
