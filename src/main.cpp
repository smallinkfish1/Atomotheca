// src/main.cpp
// 第 14 天：Emscripten / WebAssembly 适配。
//
// 主要改动：
//   1. 主循环从 while 改为可被 Emscripten 接管的回调（emscripten_set_main_loop）
//   2. 所有跨帧状态收进全局 AppState（Web 上 main 的栈会被回收，局部变量不能留）
//   3. 浏览器里没有命令行参数，控制面板加"示例分子"下拉框
//   4. OpenGL 头文件按平台切换（桌面 GLAD / Web GLES3）
//
// 桌面与 Web 的功能完全一致：球棍/填充、光照、拾取、键长键角。

// ---------- 平台相关的头 ----------
#ifdef __EMSCRIPTEN__
#  include <GLES3/gl3.h>
#  include <emscripten.h>
#  include <emscripten/html5.h>
#else
#  include <glad/glad.h>
#endif

#include <GLFW/glfw3.h>

#include "shader.h"
#include "sphere.h"
#include "cylinder.h"
#include "molecule.h"
#include "molfile.h"
#include "picking.h"
#include "path_utils.h"
#include "file_dialog.h"     // 跨平台文件选择对话框（拖拽之外的第二入口）

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifndef APP_VERSION
#  define APP_VERSION "0.14.0-dev"
#endif

const unsigned int SCR_WIDTH  = 1100;
const unsigned int SCR_HEIGHT = 700;
const float        FOV_DEG    = 45.0f;

// ==================== UI 可调状态 ====================
struct UiState
{
    int   renderMode = 0;          // 0 = 球棍, 1 = 空间填充
    float ballScale  = 1.0f;
    float vdwScale   = 0.98f;

    bool  autoRotate  = false;
    float rotateSpeed = 0.6f;

    float ambientStrength  = 0.35f;
    float specularStrength = 0.55f;
    float shininess        = 64.0f;
    float fillStrength     = 1.0f;

    float bgColor[3] = { 0.08f, 0.08f, 0.13f };

    bool showPanel          = true;
    bool showSelectedLabels = true;
    bool showAllLabels      = false;
};

UiState g_ui;

// ==================== 应用状态（跨帧）====================
// Web 上 main 的栈帧会被回收，所以所有需要跨帧存活的东西必须放全局。
struct AppState
{
    GLFWwindow* window = nullptr;

    Molecule                 molecule;
    std::vector<std::string> elements;
    std::string              fileName;
    std::string              filePath;

    Shader* sphereShader   = nullptr;
    Shader* cylinderShader = nullptr;

    unsigned int sphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
    unsigned int cylVAO    = 0, cylVBO    = 0, cylEBO    = 0;

    GLsizei sphereIndexCount = 0;
    GLsizei cylIndexCount    = 0;

    // sphere uniforms
    GLint s_model = -1, s_view = -1, s_proj = -1, s_normalMat = -1;
    GLint s_objColor = -1, s_lightColor = -1, s_lightPos = -1;
    GLint s_fillPos = -1, s_fillColor = -1, s_viewPos = -1, s_normalOff = -1;
    GLint s_ambient = -1, s_specular = -1, s_shiny = -1;

    // cylinder uniforms
    GLint c_model = -1, c_view = -1, c_proj = -1, c_normalMat = -1;
    GLint c_objColor = -1, c_lightColor = -1, c_lightPos = -1;
    GLint c_fillPos = -1, c_fillColor = -1, c_viewPos = -1;
    GLint c_ambient = -1, c_specular = -1, c_shiny = -1;

    std::vector<std::pair<float, int>> drawOrder;

    float molR = 1.0f;

    // ---------- 文件加载结果提示 ----------
    // 加载失败时不能只往 stderr 打，用户在图形界面里看不到终端。
    // 所以在面板上显示一段时间，成功（绿）/失败（红）用颜色区分。
    std::string statusMsg;
    bool        statusIsError = false;
    double      statusTime    = 0.0;   // 显示到这个时刻为止（glfwGetTime）
};

AppState g_app;

// ==================== 选中状态 ====================
std::vector<int> g_selected;
const size_t     MAX_SELECTION = 2;

// ==================== 鼠标状态 ====================
float g_mouseX = SCR_WIDTH  / 2.0f;
float g_mouseY = SCR_HEIGHT / 2.0f;

bool  g_leftPressed = false;
float g_pressX = 0.0f;
float g_pressY = 0.0f;

const float CLICK_THRESHOLD = 5.0f;

// ==================== 其他全局 ====================
float g_autoAngle = 0.0f;
float g_lastFrame = 0.0f;

glm::mat4 g_spinMatrix = glm::mat4(1.0f);

int g_winW = SCR_WIDTH;
int g_winH = SCR_HEIGHT;

// ==================== 光照常量 ====================
const glm::vec3 KEY_LIGHT_OFFSET  = glm::vec3( 2.0f,  2.0f, 0.0f);
const glm::vec3 FILL_LIGHT_OFFSET = glm::vec3(-2.5f, -1.5f, 0.0f);
const glm::vec3 FILL_LIGHT_BASE   = glm::vec3( 0.38f,  0.38f, 0.45f);

const glm::vec3 BOND_COLOR  = glm::vec3(0.62f, 0.62f, 0.66f);
const float     BOND_RADIUS = 0.07f;

const glm::vec3 HIGHLIGHT_COLOR = glm::vec3(1.00f, 0.80f, 0.15f);

// ==================== 相机 ====================
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

// ==================== Web 版：内置示例分子 ====================
// 浏览器里没有命令行参数，所以用下拉框切换
#ifdef __EMSCRIPTEN__
static const char* WEB_MOLECULES[] = {
    "assets/methane.mol",
    "assets/methanol.mol",
    "assets/ethanol.mol",
    "assets/benzene.mol",
    "assets/water.mol",
};
static const char* WEB_LABELS[] = {
    "Methane  (CH4)",
    "Methanol (CH3OH)",
    "Ethanol  (C2H5OH)",
    "Benzene  (C6H6)",
    "Water    (H2O)",
};
static const int WEB_MOLECULE_COUNT = 5;
static int g_webMolIndex = 0;
#endif

// ==================== ImGui 状态查询 ====================
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

// ==================== 工具 ====================
static float displayRadius(const Atom& atom)
{
    ElementStyle st = getElementStyle(atom.element);
    return (g_ui.renderMode == 0)
               ? st.ballRadius * g_ui.ballScale
               : st.vdwRadius  * g_ui.vdwScale;
}

static glm::vec3 atomWorldPos(const Atom& atom)
{
    return glm::vec3(g_spinMatrix * glm::vec4(atom.position, 1.0f));
}

static bool isSelected(int idx)
{
    return std::find(g_selected.begin(), g_selected.end(), idx) != g_selected.end();
}

// ==================== 状态提示 ====================
// 面板上的一行反馈。错误停留久一点，方便看清原因。
static void setStatus(const std::string& msg, bool isError)
{
    g_app.statusMsg     = msg;
    g_app.statusIsError = isError;
    g_app.statusTime    = glfwGetTime() + (isError ? 12.0 : 5.0);
}

// 只取路径里的文件名部分（面板显示用）
static std::string baseName(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// ==================== 加载分子（桌面和 Web 共用）====================
static bool loadMoleculeFile(const std::string& path)
{
    // 先解析到临时对象：失败时不会破坏当前正在显示的分子的任何状态
    Molecule m;
    if (!molfile::loadMolecule(path, m) || m.atoms.empty())
    {
        setStatus("无法加载 " + baseName(path) + "（格式不支持或文件损坏）", true);
        std::cerr << "[错误] 加载失败: " << path << std::endl;
        return false;
    }

    g_app.molecule = std::move(m);
    g_app.filePath = path;

    // 只保留文件名，用于面板显示
    g_app.fileName = baseName(path);

    // 统计元素
    g_app.elements.clear();
    {
        std::map<std::string, int> counts;
        for (const auto& a : g_app.molecule.atoms)
            counts[a.element]++;
        for (const auto& kv : counts)
            g_app.elements.push_back(kv.first);
    }

    // 自动相机距离
    g_app.molR = molfile::moleculeRadius(g_app.molecule);
    if (g_app.molR < 0.1f) g_app.molR = 1.0f;

    float dist = g_app.molR / std::tan(glm::radians(FOV_DEG) * 0.5f) * 1.55f;
    g_radiusMin = dist * 0.12f;
    g_radiusMax = dist * 5.0f;
    g_radius    = dist;

    // 选中下标是旧分子的，必须清掉，否则新分子原子更少时会越界
    g_selected.clear();

    setStatus("已加载 " + g_app.fileName + "（" +
              std::to_string(g_app.molecule.atoms.size()) + " 原子，" +
              std::to_string(g_app.molecule.bonds.size()) + " 键）", false);

    return true;
}

// ==================== 拖拽打开文件 ====================
// 扩展名判断不区分大小写（Windows 上可能是 .MOL）
static bool hasMoleculeExtension(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size())
        return false;

    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return ext == "mol" || ext == "pdb";
}

// GLFW 会把拖进来的文件以 UTF-8 绝对路径传进来；可能一次拖多个
static void drop_callback(GLFWwindow* /*window*/, int count, const char** paths)
{
    if (count <= 0 || paths == nullptr)
        return;

    bool tried = false;

    for (int i = 0; i < count; ++i)
    {
        if (paths[i] == nullptr) continue;

        std::string path = paths[i];
        if (!hasMoleculeExtension(path)) continue;

        tried = true;
        if (loadMoleculeFile(path))
            return;                       // 成功一个就够了
    }

    // 如果是"解析失败"，loadMoleculeFile 已经写过具体原因，不要覆盖
    if (!tried)
        setStatus("拖入的文件不是 .mol / .pdb 格式", true);
}

// ==================== 拾取 ====================
static void doPick(float sx, float sy, bool additive,
                   const glm::mat4& view, const glm::mat4& projection)
{
    if (g_app.molecule.atoms.empty()) return;

    picking::Ray ray = picking::makeRay(
        sx, sy,
        static_cast<float>(g_winW), static_cast<float>(g_winH),
        view, projection);

    int   hitIndex = -1;
    float nearestT = 1e30f;

    for (int i = 0; i < static_cast<int>(g_app.molecule.atoms.size()); ++i)
    {
        const Atom& atom = g_app.molecule.atoms[i];
        float t = picking::raySphere(ray, atomWorldPos(atom), displayRadius(atom));
        if (t > 0.0f && t < nearestT)
        {
            nearestT = t;
            hitIndex = i;
        }
    }

    if (hitIndex < 0)
    {
        if (!additive) g_selected.clear();
        return;
    }

    if (!additive)
    {
        g_selected.clear();
        g_selected.push_back(hitIndex);
    }
    else
    {
        auto it = std::find(g_selected.begin(), g_selected.end(), hitIndex);
        if (it != g_selected.end())
            g_selected.erase(it);
        else
        {
            if (g_selected.size() >= MAX_SELECTION)
                g_selected.erase(g_selected.begin());
            g_selected.push_back(hitIndex);
        }
    }
}

// ==================== 回调 ====================
static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    g_mouseX = (float)xpos;
    g_mouseY = (float)ypos;

    if (g_firstMouse)
    {
        g_lastX = g_mouseX;
        g_lastY = g_mouseY;
        g_firstMouse = false;
        return;
    }

    float xoffset = g_mouseX - g_lastX;
    float yoffset = g_lastY - g_mouseY;
    g_lastX = g_mouseX;
    g_lastY = g_mouseY;

    if (imguiWantsMouse()) return;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) return;

    xoffset *= SENSITIVITY;
    yoffset *= SENSITIVITY;

    g_yaw   += xoffset;
    g_pitch += yoffset;

    if (g_pitch >  PITCH_LIMIT) g_pitch =  PITCH_LIMIT;
    if (g_pitch < -PITCH_LIMIT) g_pitch = -PITCH_LIMIT;
}

static void mouse_button_callback(GLFWwindow* /*window*/, int button,
                                  int action, int mods)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (imguiWantsMouse()) return;

    if (action == GLFW_PRESS)
    {
        g_leftPressed = true;
        g_pressX = g_mouseX;
        g_pressY = g_mouseY;
    }
    else if (action == GLFW_RELEASE && g_leftPressed)
    {
        g_leftPressed = false;

        float dx = g_mouseX - g_pressX;
        float dy = g_mouseY - g_pressY;

        if (dx * dx + dy * dy <= CLICK_THRESHOLD * CLICK_THRESHOLD)
        {
            float camX = g_radius * std::cos(g_pitch) * std::sin(g_yaw);
            float camY = g_radius * std::sin(g_pitch);
            float camZ = g_radius * std::cos(g_pitch) * std::cos(g_yaw);
            glm::vec3 camPos(camX, camY, camZ);

            glm::mat4 view = glm::lookAt(camPos,
                                         glm::vec3(0.0f, 0.0f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 projection = glm::perspective(
                glm::radians(FOV_DEG),
                static_cast<float>(g_winW) / static_cast<float>(g_winH),
                0.1f, 500.0f);

            doPick(g_mouseX, g_mouseY, (mods & GLFW_MOD_SHIFT) != 0,
                   view, projection);
        }
    }
}

static void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    if (imguiWantsMouse()) return;

    float factor = 1.0f - (float)yoffset * 0.12f;
    g_radius *= factor;

    if (g_radius < g_radiusMin) g_radius = g_radiusMin;
    if (g_radius > g_radiusMax) g_radius = g_radiusMax;
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/,
                         int action, int mods)
{
    if (action != GLFW_PRESS) return;
    (void)mods;   // Web 构建下不会用到

#ifndef __EMSCRIPTEN__
    // Ctrl+O：打开文件。
    // 这一条要放在 ImGui 焦点判断【之前】——否则刚点过面板、
    // ImGui 还握着键盘焦点时，快捷键就按不出来了。
    if (key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL))
    {
        std::string picked = filedialog::openMoleculeDialog("打开分子文件");
        if (!picked.empty())
            loadMoleculeFile(picked);
        else if (!filedialog::lastError().empty())
            setStatus(filedialog::lastError(), true);
        return;
    }
#endif

    if (imguiWantsKeyboard()) return;

    if (key == GLFW_KEY_SPACE)
    {
        g_ui.renderMode = (g_ui.renderMode == 0) ? 1 : 0;
    }
    else if (key == GLFW_KEY_TAB)
    {
        g_ui.showPanel = !g_ui.showPanel;
    }
    else if (key == GLFW_KEY_ESCAPE)
    {
        if (!g_selected.empty())
            g_selected.clear();
#ifndef __EMSCRIPTEN__
        else
            glfwSetWindowShouldClose(window, true);
#endif
        // Web 上 Esc 不关页面（浏览器里没有"关窗口"这回事）
    }
}

static void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height)
{
    glViewport(0, 0, width, height);
}

// ==================== 键的 Model 矩阵 ====================
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

// ==================== 邻居与键角 ====================
static std::vector<int> neighborsOf(const Molecule& mol, int index)
{
    std::vector<int> result;
    for (const auto& b : mol.bonds)
    {
        if (b.a == index)      result.push_back(b.b);
        else if (b.b == index) result.push_back(b.a);
    }
    return result;
}

static float bondAngle(const Molecule& mol, int a, int mid, int b)
{
    glm::vec3 v1 = mol.atoms[a].position   - mol.atoms[mid].position;
    glm::vec3 v2 = mol.atoms[b].position   - mol.atoms[mid].position;

    float len1 = glm::length(v1);
    float len2 = glm::length(v2);
    if (len1 < 1e-6f || len2 < 1e-6f) return 0.0f;

    float c = glm::clamp(glm::dot(v1 / len1, v2 / len2), -1.0f, 1.0f);
    return glm::degrees(std::acos(c));
}

// ==================== 选择信息 ====================
static void DrawSelectionInfo(const Molecule& mol)
{
    if (g_selected.empty())
    {
        ImGui::TextDisabled("Click an atom to inspect");
        ImGui::TextDisabled("Shift+Click to add a 2nd atom");
        return;
    }

    if (g_selected.size() == 1)
    {
        int idx = g_selected[0];
        const Atom& atom = mol.atoms[idx];

        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
                           "%s%d   (index %d)",
                           atom.element.c_str(), idx + 1, idx);
        ImGui::Text("Pos  %.3f  %.3f  %.3f",
                    atom.position.x, atom.position.y, atom.position.z);

        std::vector<int> nbrs = neighborsOf(mol, idx);
        ImGui::Text("Neighbors  %d", static_cast<int>(nbrs.size()));

        if (!nbrs.empty() && ImGui::TreeNode("Bond lengths"))
        {
            for (int n : nbrs)
            {
                float d = glm::length(mol.atoms[n].position - atom.position);
                ImGui::Text("%s%d-%s%d : %.3f A",
                            atom.element.c_str(), idx + 1,
                            mol.atoms[n].element.c_str(), n + 1, d);
            }
            ImGui::TreePop();
        }

        if (nbrs.size() >= 2 && ImGui::TreeNode("Bond angles"))
        {
            for (size_t i = 0; i < nbrs.size(); ++i)
            {
                for (size_t j = i + 1; j < nbrs.size(); ++j)
                {
                    float deg = bondAngle(mol, nbrs[i], idx, nbrs[j]);
                    ImGui::Text("%s%d-%s%d-%s%d : %.1f deg",
                                mol.atoms[nbrs[i]].element.c_str(), nbrs[i] + 1,
                                atom.element.c_str(), idx + 1,
                                mol.atoms[nbrs[j]].element.c_str(), nbrs[j] + 1,
                                deg);
                }
            }
            ImGui::TreePop();
        }
    }
    else
    {
        int ia = g_selected[0];
        int ib = g_selected[1];
        const Atom& a = mol.atoms[ia];
        const Atom& b = mol.atoms[ib];

        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
                           "%s%d  --  %s%d",
                           a.element.c_str(), ia + 1,
                           b.element.c_str(), ib + 1);
        ImGui::Text("Distance  %.3f A", glm::length(b.position - a.position));

        std::vector<int> na = neighborsOf(mol, ia);
        std::vector<int> nb = neighborsOf(mol, ib);

        for (int c : na)
        {
            if (std::find(nb.begin(), nb.end(), c) != nb.end())
            {
                float deg = bondAngle(mol, ia, c, ib);
                ImGui::Text("Angle %s%d-%s%d-%s%d : %.1f deg",
                            a.element.c_str(), ia + 1,
                            mol.atoms[c].element.c_str(), c + 1,
                            b.element.c_str(), ib + 1,
                            deg);
            }
        }

        if (std::find(na.begin(), na.end(), ib) != na.end())
            ImGui::TextDisabled("(directly bonded)");
    }

    if (ImGui::Button("Clear selection"))
        g_selected.clear();
}

// ==================== 控制面板 ====================
static void DrawControlPanel()
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f),  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90f);

    if (ImGui::Begin("Control Panel", &g_ui.showPanel))
    {
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "MOLECULE");
        ImGui::Separator();

#ifdef __EMSCRIPTEN__
        // Web 版：下拉框切换示例分子
        if (ImGui::Combo("Example", &g_webMolIndex, WEB_LABELS, WEB_MOLECULE_COUNT))
        {
            if (!loadMoleculeFile(WEB_MOLECULES[g_webMolIndex]))
                std::cerr << "[错误] 加载失败: " << WEB_MOLECULES[g_webMolIndex] << std::endl;
        }
#else
        ImGui::Text("File   %s", g_app.fileName.c_str());

        // 入口二：系统文件对话框（入口一是把文件拖进窗口）
        if (ImGui::Button("Open .mol / .pdb ..."))
        {
            std::string picked = filedialog::openMoleculeDialog("打开分子文件");
            if (!picked.empty())
                loadMoleculeFile(picked);
            else if (!filedialog::lastError().empty())
                setStatus(filedialog::lastError(), true);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+O");

        // 结果提示：成功 5 秒、失败 12 秒；过期后换成拖拽提示
        if (!g_app.statusMsg.empty() && glfwGetTime() < g_app.statusTime)
        {
            ImVec4 col = g_app.statusIsError
                             ? ImVec4(1.00f, 0.45f, 0.40f, 1.0f)
                             : ImVec4(0.55f, 0.90f, 0.60f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s", g_app.statusMsg.c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextDisabled("Or drop a .mol / .pdb onto the window");
        }
#endif
        ImGui::Text("Atoms  %d", static_cast<int>(g_app.molecule.atoms.size()));
        ImGui::Text("Bonds  %d", static_cast<int>(g_app.molecule.bonds.size()));
        ImGui::Text("Radius %.2f A", g_app.molR);
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "SELECTION");
        ImGui::Separator();
        DrawSelectionInfo(g_app.molecule);
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "DISPLAY");
        ImGui::Separator();
        ImGui::RadioButton("Ball-and-Stick", &g_ui.renderMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Space-Filling",  &g_ui.renderMode, 1);

        if (g_ui.renderMode == 0)
            ImGui::SliderFloat("Atom radius", &g_ui.ballScale, 0.30f, 2.00f, "%.2f x");
        else
            ImGui::SliderFloat("Fill factor", &g_ui.vdwScale,  0.80f, 1.10f, "%.2f");

        ImGui::Checkbox("Labels (selected)",  &g_ui.showSelectedLabels);
        ImGui::Checkbox("Labels (all atoms)", &g_ui.showAllLabels);
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "ANIMATION");
        ImGui::Separator();
        ImGui::Checkbox("Auto rotate", &g_ui.autoRotate);
        if (g_ui.autoRotate)
            ImGui::SliderFloat("Speed", &g_ui.rotateSpeed, 0.0f, 3.0f, "%.2f rad/s");
        ImGui::Spacing();

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

        if (ImGui::CollapsingHeader("ELEMENT LEGEND", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const auto& el : g_app.elements)
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
        ImGui::TextDisabled("Drag LMB  : rotate view");
        ImGui::TextDisabled("Click     : select atom");
        ImGui::TextDisabled("Shift+Clk : add 2nd atom");
        ImGui::TextDisabled("Scroll    : zoom");
#ifndef __EMSCRIPTEN__
        ImGui::TextDisabled("Ctrl+O    : open file");
        ImGui::TextDisabled("Drop file : open molecule");
#endif
        ImGui::TextDisabled("Space     : switch model");
        ImGui::TextDisabled("Tab       : show / hide panel");
        ImGui::TextDisabled("Esc       : clear selection");
    }
    ImGui::End();
}

// ==================== 3D 标签 ====================
static void DrawAtomLabels(const glm::mat4& view, const glm::mat4& projection)
{
    if (!g_ui.showSelectedLabels && !g_ui.showAllLabels)
        return;

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (fg == nullptr) return;

    for (int i = 0; i < static_cast<int>(g_app.molecule.atoms.size()); ++i)
    {
        bool sel = isSelected(i);
        if (!sel && !g_ui.showAllLabels) continue;

        glm::vec3 wp = atomWorldPos(g_app.molecule.atoms[i]);

        float sx = 0.0f, sy = 0.0f;
        if (!picking::worldToScreen(wp, view, projection,
                                    static_cast<float>(g_winW),
                                    static_cast<float>(g_winH),
                                    sx, sy))
            continue;

        if (sx < -60.0f || sy < -60.0f ||
            sx > static_cast<float>(g_winW) + 60.0f ||
            sy > static_cast<float>(g_winH) + 60.0f)
            continue;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%d",
                      g_app.molecule.atoms[i].element.c_str(), i + 1);

        ImVec2 textSize = ImGui::CalcTextSize(buf);
        ImVec2 pos(sx - textSize.x * 0.5f, sy - 30.0f);

        ImU32 bgCol   = sel ? IM_COL32(60, 45, 10, 230)
                            : IM_COL32(20, 20, 30, 200);
        ImU32 textCol = sel ? IM_COL32(255, 220, 120, 255)
                            : IM_COL32(200, 200, 210, 220);

        fg->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 2.0f),
                          ImVec2(pos.x + textSize.x + 4.0f, pos.y + textSize.y + 2.0f),
                          bgCol, 3.0f);

        if (sel)
            fg->AddRect(ImVec2(pos.x - 4.0f, pos.y - 2.0f),
                        ImVec2(pos.x + textSize.x + 4.0f, pos.y + textSize.y + 2.0f),
                        IM_COL32(255, 200, 80, 255), 3.0f);

        fg->AddText(pos, textCol, buf);
    }
}

// ============================================================
//  一帧的全部工作
//
//  桌面：由 while 循环反复调用
//  Web ：由 emscripten_set_main_loop 反复调用
//
//  所以它不能有任何"局部持久状态"——所有跨帧数据都在 g_app / 全局变量里
// ============================================================
static void renderFrame()
{
    AppState& app = g_app;

    float currentFrame = (float)glfwGetTime();
    float deltaTime = currentFrame - g_lastFrame;
    g_lastFrame = currentFrame;
    if (deltaTime > 0.1f) deltaTime = 0.1f;

    glfwPollEvents();
    glfwGetWindowSize(app.window, &g_winW, &g_winH);
    if (g_winW <= 0 || g_winH <= 0) return;

    if (g_ui.autoRotate)
        g_autoAngle += g_ui.rotateSpeed * deltaTime;

    g_spinMatrix = glm::mat4(1.0f);
    if (g_autoAngle != 0.0f)
        g_spinMatrix = glm::rotate(glm::mat4(1.0f), g_autoAngle,
                                   glm::vec3(0.0f, 1.0f, 0.0f));

    // ---------- ImGui ----------
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (g_ui.showPanel)
        DrawControlPanel();

    // ---------- 相机 ----------
    // 注意：必须放在面板之后。
    // 面板里可能刚加载了新分子（改了 g_radius / app.molR），
    // 如果相机参数在面板之前算，加载后的第一帧会用旧距离，出现一帧缩放错乱。
    float camX = g_radius * std::cos(g_pitch) * std::sin(g_yaw);
    float camY = g_radius * std::sin(g_pitch);
    float camZ = g_radius * std::cos(g_pitch) * std::cos(g_yaw);
    g_cameraPos = glm::vec3(camX, camY, camZ);

    glm::mat4 view = glm::lookAt(g_cameraPos,
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 projection = glm::perspective(
        glm::radians(FOV_DEG),
        static_cast<float>(g_winW) / static_cast<float>(g_winH),
        0.1f, 500.0f);

    float lightScale = std::max(1.0f, app.molR);
    glm::vec3 lightPos     = g_cameraPos + KEY_LIGHT_OFFSET  * lightScale;
    glm::vec3 fillLightPos = g_cameraPos + FILL_LIGHT_OFFSET * lightScale;
    glm::vec3 fillColor    = FILL_LIGHT_BASE * g_ui.fillStrength;

    DrawAtomLabels(view, projection);

    ImGui::Render();

    // ---------- 场景 ----------
    glClearColor(g_ui.bgColor[0], g_ui.bgColor[1], g_ui.bgColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    app.drawOrder.clear();
    for (int i = 0; i < static_cast<int>(app.molecule.atoms.size()); ++i)
    {
        float d = glm::length(atomWorldPos(app.molecule.atoms[i]) - g_cameraPos);
        app.drawOrder.push_back({ d, i });
    }
    std::sort(app.drawOrder.begin(), app.drawOrder.end(),
              [](const std::pair<float, int>& a, const std::pair<float, int>& b)
              {
                  return a.first > b.first;
              });

    // ---------- 原子 ----------
    app.sphereShader->use();

    glUniformMatrix4fv(app.s_view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(app.s_proj, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(app.s_lightColor, 1.0f, 1.0f, 1.0f);
    glUniform3f(app.s_lightPos, lightPos.x, lightPos.y, lightPos.z);
    glUniform3f(app.s_fillColor, fillColor.r, fillColor.g, fillColor.b);
    glUniform3f(app.s_fillPos, fillLightPos.x, fillLightPos.y, fillLightPos.z);
    glUniform3f(app.s_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);
    glUniform1f(app.s_ambient,  g_ui.ambientStrength);
    glUniform1f(app.s_specular, g_ui.specularStrength);
    glUniform1f(app.s_shiny,    g_ui.shininess);
    glUniform1f(app.s_normalOff, (g_ui.renderMode == 1) ? -0.002f : 0.0f);

    glBindVertexArray(app.sphereVAO);

    for (const auto& pair : app.drawOrder)
    {
        int idx = pair.second;
        const Atom& atom = app.molecule.atoms[idx];

        float r = displayRadius(atom);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, atom.position);
        model = glm::scale(model, glm::vec3(r));
        model = g_spinMatrix * model;

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

        glm::vec3 color = getElementStyle(atom.element).color;
        if (isSelected(idx))
            color = glm::mix(color, HIGHLIGHT_COLOR, 0.60f);

        glUniformMatrix4fv(app.s_model, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(app.s_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        glUniform3f(app.s_objColor, color.r, color.g, color.b);

        glDrawElements(GL_TRIANGLES, app.sphereIndexCount, GL_UNSIGNED_INT, 0);
    }

    // ---------- 键 ----------
    if (g_ui.renderMode == 0)
    {
        app.cylinderShader->use();

        glUniformMatrix4fv(app.c_view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(app.c_proj, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3f(app.c_lightColor, 1.0f, 1.0f, 1.0f);
        glUniform3f(app.c_lightPos, lightPos.x, lightPos.y, lightPos.z);
        glUniform3f(app.c_fillColor, fillColor.r, fillColor.g, fillColor.b);
        glUniform3f(app.c_fillPos, fillLightPos.x, fillLightPos.y, fillLightPos.z);
        glUniform3f(app.c_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);
        glUniform1f(app.c_ambient,  g_ui.ambientStrength);
        glUniform1f(app.c_specular, g_ui.specularStrength);
        glUniform1f(app.c_shiny,    g_ui.shininess);

        glBindVertexArray(app.cylVAO);

        for (const auto& bond : app.molecule.bonds)
        {
            glm::vec3 a = app.molecule.atoms[bond.a].position;
            glm::vec3 b = app.molecule.atoms[bond.b].position;

            bool touched = isSelected(bond.a) || isSelected(bond.b);
            glm::vec3 bc = touched
                         ? glm::mix(BOND_COLOR, HIGHLIGHT_COLOR, 0.45f)
                         : BOND_COLOR;
            glUniform3f(app.c_objColor, bc.r, bc.g, bc.b);

            glm::mat4 model = makeBondModel(a, b);
            model = glm::scale(model, glm::vec3(BOND_RADIUS, 1.0f, BOND_RADIUS));
            model = g_spinMatrix * model;

            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

            glUniformMatrix4fv(app.c_model, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(app.c_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));

            glDrawElements(GL_TRIANGLES, app.cylIndexCount, GL_UNSIGNED_INT, 0);
        }
    }

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(app.window);
}

// ==================== 初始化网格 ====================
static void setupMeshes()
{
    AppState& app = g_app;

    std::vector<float> sphereVerts;
    std::vector<unsigned int> sphereIdx;
    generateSphere(1.0f, 32, 16, sphereVerts, sphereIdx);

    std::vector<float> cylVerts;
    std::vector<unsigned int> cylIdx;
    generateCylinder(1.0f, 20, cylVerts, cylIdx);

    app.sphereIndexCount = static_cast<GLsizei>(sphereIdx.size());
    app.cylIndexCount    = static_cast<GLsizei>(cylIdx.size());

    glGenVertexArrays(1, &app.sphereVAO);
    glGenBuffers(1, &app.sphereVBO);
    glGenBuffers(1, &app.sphereEBO);
    glBindVertexArray(app.sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sphereVerts.size() * sizeof(float),
                 sphereVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphereIdx.size() * sizeof(unsigned int),
                 sphereIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glGenVertexArrays(1, &app.cylVAO);
    glGenBuffers(1, &app.cylVBO);
    glGenBuffers(1, &app.cylEBO);
    glBindVertexArray(app.cylVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.cylVBO);
    glBufferData(GL_ARRAY_BUFFER, cylVerts.size() * sizeof(float),
                 cylVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.cylEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cylIdx.size() * sizeof(unsigned int),
                 cylIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// ==================== main ====================
int main(int argc, char** argv)
{
    // ---------- 命令行（Web 上没有）----------
    std::string userPath = "assets/methane.mol";

#ifndef __EMSCRIPTEN__
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            std::cout
                << "Molecule Viewer v" << APP_VERSION << "\n\n"
                << "用法: molecule_viewer [分子文件]\n\n"
                << "  [分子文件]   支持 .mol (V2000) 和 .pdb\n"
                << "               省略时默认加载 assets/methane.mol\n\n"
                << "操作:\n"
                << "  左键拖拽     旋转视角\n"
                << "  左键单击     选中原子\n"
                << "  Shift+单击   追加选中第二个原子\n"
                << "  滚轮         缩放\n"
                << "  空格         切换 球棍 / 空间填充 模型\n"
                << "  Tab          显示 / 隐藏控制面板\n"
                << "  Esc          取消选择；没有选中时退出\n";
            return 0;
        }
        if (arg == "-v" || arg == "--version")
        {
            std::cout << "Molecule Viewer v" << APP_VERSION << "\n";
            return 0;
        }

        userPath = arg;
    }
#endif

    // ---------- 资源路径 ----------
    std::string molPath = pathutil::resolveResource(userPath);
    if (molPath.empty())
    {
        std::cerr << "[错误] 找不到分子文件: " << userPath << "\n";
#ifndef __EMSCRIPTEN__
        std::cerr << "       当前工作目录: " << std::filesystem::current_path() << "\n";
#endif
        return -1;
    }

    // ---------- GLFW ----------
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
                                          "Molecule Viewer - Day 14", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建窗口失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    g_app.window = window;

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

#ifndef __EMSCRIPTEN__
    // 拖拽文件进窗口即加载（Emscripten 的 GLFW 移植没有实现 drop，仅桌面版注册）
    glfwSetDropCallback(window, drop_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "[错误] GLAD 初始化失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Molecule Viewer v" << APP_VERSION << std::endl;
    std::cout << "OpenGL: " << glGetString(GL_VERSION)
              << " | " << glGetString(GL_RENDERER) << std::endl;
#else
    std::cout << "Molecule Viewer v" << APP_VERSION << " (WebAssembly)" << std::endl;
    std::cout << "OpenGL ES: " << glGetString(GL_VERSION) << std::endl;
#endif

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ---------- ImGui ----------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

#ifdef __EMSCRIPTEN__
    // Web 上让 ImGui 跟随 canvas 尺寸
    io.DisplaySize = ImVec2(static_cast<float>(SCR_WIDTH),
                            static_cast<float>(SCR_HEIGHT));
#endif

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    // ---------- 加载分子 ----------
    if (!loadMoleculeFile(molPath))
    {
        std::cerr << "[错误] 加载分子文件失败，程序退出。" << std::endl;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    std::cout << "已加载: " << g_app.fileName << std::endl;
    std::cout << "  原子数: " << g_app.molecule.atoms.size()
              << "，键数: " << g_app.molecule.bonds.size() << std::endl;
    std::cout << "  元素组成: ";
    for (const auto& el : g_app.elements) std::cout << el << " ";
    std::cout << std::endl;
    std::cout << "  分子半径: " << g_app.molR
              << "，相机距离: " << g_radius << std::endl;

    // ---------- 网格 ----------
    setupMeshes();

    // ---------- 着色器 ----------
    std::string svPath = pathutil::resolveResource("src/shaders/sphere.vert");
    std::string sfPath = pathutil::resolveResource("src/shaders/sphere.frag");
    std::string cvPath = pathutil::resolveResource("src/shaders/cylinder.vert");
    std::string cfPath = pathutil::resolveResource("src/shaders/cylinder.frag");

    if (svPath.empty() || sfPath.empty() || cvPath.empty() || cfPath.empty())
    {
        std::cerr << "[错误] 找不到着色器文件。" << std::endl;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    g_app.sphereShader   = new Shader(svPath.c_str(), sfPath.c_str());
    g_app.cylinderShader = new Shader(cvPath.c_str(), cfPath.c_str());

    // ---------- 缓存 uniform 位置 ----------
    {
        GLuint sid = g_app.sphereShader->ID;
        g_app.s_model      = glGetUniformLocation(sid, "model");
        g_app.s_view       = glGetUniformLocation(sid, "view");
        g_app.s_proj       = glGetUniformLocation(sid, "projection");
        g_app.s_normalMat  = glGetUniformLocation(sid, "normalMatrix");
        g_app.s_objColor   = glGetUniformLocation(sid, "objectColor");
        g_app.s_lightColor = glGetUniformLocation(sid, "lightColor");
        g_app.s_lightPos   = glGetUniformLocation(sid, "lightPos");
        g_app.s_fillPos    = glGetUniformLocation(sid, "fillLightPos");
        g_app.s_fillColor  = glGetUniformLocation(sid, "fillLightColor");
        g_app.s_viewPos    = glGetUniformLocation(sid, "viewPos");
        g_app.s_normalOff  = glGetUniformLocation(sid, "normalOffset");
        g_app.s_ambient    = glGetUniformLocation(sid, "ambientStrength");
        g_app.s_specular   = glGetUniformLocation(sid, "specularStrength");
        g_app.s_shiny      = glGetUniformLocation(sid, "shininess");

        GLuint cid = g_app.cylinderShader->ID;
        g_app.c_model      = glGetUniformLocation(cid, "model");
        g_app.c_view       = glGetUniformLocation(cid, "view");
        g_app.c_proj       = glGetUniformLocation(cid, "projection");
        g_app.c_normalMat  = glGetUniformLocation(cid, "normalMatrix");
        g_app.c_objColor   = glGetUniformLocation(cid, "objectColor");
        g_app.c_lightColor = glGetUniformLocation(cid, "lightColor");
        g_app.c_lightPos   = glGetUniformLocation(cid, "lightPos");
        g_app.c_fillPos    = glGetUniformLocation(cid, "fillLightPos");
        g_app.c_fillColor  = glGetUniformLocation(cid, "fillLightColor");
        g_app.c_viewPos    = glGetUniformLocation(cid, "viewPos");
        g_app.c_ambient    = glGetUniformLocation(cid, "ambientStrength");
        g_app.c_specular   = glGetUniformLocation(cid, "specularStrength");
        g_app.c_shiny      = glGetUniformLocation(cid, "shininess");
    }

    g_lastFrame = (float)glfwGetTime();

    // ==================== 启动主循环 ====================
#ifdef __EMSCRIPTEN__
    // Web：交给浏览器驱动。
    //   第 2 个参数 0 = 跟随显示器刷新率（requestAnimationFrame）
    //   第 3 个参数 1 = 不返回（内部用异常阻止 main 继续执行）
    //   ★ 正因如此，渲染所需的所有状态必须在全局，不能是 main 的局部变量
    emscripten_set_main_loop(renderFrame, 0, 1);
#else
    while (!glfwWindowShouldClose(window))
        renderFrame();
#endif

    // ==================== 清理（Web 上不会执行到这里）====================
    delete g_app.sphereShader;
    delete g_app.cylinderShader;
    g_app.sphereShader = nullptr;
    g_app.cylinderShader = nullptr;

    glDeleteVertexArrays(1, &g_app.sphereVAO);
    glDeleteBuffers(1, &g_app.sphereVBO);
    glDeleteBuffers(1, &g_app.sphereEBO);
    glDeleteVertexArrays(1, &g_app.cylVAO);
    glDeleteBuffers(1, &g_app.cylVBO);
    glDeleteBuffers(1, &g_app.cylEBO);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
