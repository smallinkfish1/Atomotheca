// src/main.cpp
// 第 13 天：工程化收尾。
//   - 资源路径自适配：不再要求"必须在项目根目录运行"
//   - 命令行支持 --help / --version
//   - 版本号由 CMake 注入（APP_VERSION 宏）
//
// 前 12 天的功能保持不变：球棍/填充切换、轨道相机、CPK 配色、
// Blinn-Phong 光照、.mol/.pdb 解析、ImGui 面板、射线拾取与键长键角测量。

#ifndef APP_VERSION
#  define APP_VERSION "0.13.0-dev"
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "shader.h"
#include "sphere.h"
#include "cylinder.h"
#include "molecule.h"
#include "molfile.h"
#include "picking.h"      // 第 12 天：屏幕 → 3D 射线
#include "path_utils.h"   // 第 13 天：跨平台资源路径解析

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

const unsigned int SCR_WIDTH  = 1100;
const unsigned int SCR_HEIGHT = 700;

// ==================== UI 可调状态 ====================
struct UiState
{
    // 显示
    int   renderMode = 0;          // 0 = 球棍模型, 1 = 空间填充模型
    float ballScale  = 1.0f;
    float vdwScale   = 0.98f;

    // 动画
    bool  autoRotate  = false;
    float rotateSpeed = 0.6f;

    // 光照
    float ambientStrength  = 0.35f;
    float specularStrength = 0.55f;
    float shininess        = 64.0f;
    float fillStrength     = 1.0f;

    // 背景
    float bgColor[3] = { 0.08f, 0.08f, 0.13f };

    // 面板
    bool showPanel = true;

    // 第 12 天：标签
    bool showSelectedLabels = true;
    bool showAllLabels      = false;
};

UiState g_ui;

// ==================== 选中状态（第 12 天）====================
std::vector<int> g_selected;
const size_t     MAX_SELECTION = 2;

// ==================== 鼠标状态 ====================
float g_mouseX = SCR_WIDTH  / 2.0f;
float g_mouseY = SCR_HEIGHT / 2.0f;

bool  g_leftPressed = false;
float g_pressX = 0.0f;
float g_pressY = 0.0f;

const float CLICK_THRESHOLD = 5.0f;   // 松开时移动小于 5 像素，算"点击"

// ==================== 其他全局状态 ====================
float g_autoAngle = 0.0f;
float g_lastFrame = 0.0f;

glm::mat4 g_spinMatrix = glm::mat4(1.0f);   // 自动旋转矩阵（渲染/拾取/标签共用）

int g_winW = SCR_WIDTH;
int g_winH = SCR_HEIGHT;

// ==================== 光照常量 ====================
const glm::vec3 KEY_LIGHT_OFFSET  = glm::vec3( 2.0f,  2.0f, 0.0f);
const glm::vec3 FILL_LIGHT_OFFSET = glm::vec3(-2.5f, -1.5f, 0.0f);
const glm::vec3 FILL_LIGHT_BASE   = glm::vec3( 0.38f,  0.38f, 0.45f);

const glm::vec3 BOND_COLOR  = glm::vec3(0.62f, 0.62f, 0.66f);
const float     BOND_RADIUS = 0.07f;

const glm::vec3 HIGHLIGHT_COLOR = glm::vec3(1.00f, 0.80f, 0.15f);

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

// 分子指针（回调里要用）
const Molecule* g_molecule = nullptr;

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

// 原子在世界空间的位置（要考虑自动旋转）
// 渲染、拾取、标签三处必须用同一个变换，否则点不中
static glm::vec3 atomWorldPos(const Atom& atom)
{
    return glm::vec3(g_spinMatrix * glm::vec4(atom.position, 1.0f));
}

static bool isSelected(int idx)
{
    return std::find(g_selected.begin(), g_selected.end(), idx) != g_selected.end();
}

// ==================== 拾取 ====================
static void doPick(float sx, float sy, bool additive,
                   const glm::mat4& view, const glm::mat4& projection)
{
    if (g_molecule == nullptr) return;

    picking::Ray ray = picking::makeRay(
        sx, sy,
        static_cast<float>(g_winW), static_cast<float>(g_winH),
        view, projection);

    int   hitIndex = -1;
    float nearestT = 1e30f;

    for (int i = 0; i < static_cast<int>(g_molecule->atoms.size()); ++i)
    {
        const Atom& atom = g_molecule->atoms[i];

        float t = picking::raySphere(ray, atomWorldPos(atom), displayRadius(atom));

        if (t > 0.0f && t < nearestT)
        {
            nearestT = t;
            hitIndex = i;
        }
    }

    if (hitIndex < 0)
    {
        if (!additive)
            g_selected.clear();
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
        {
            g_selected.erase(it);
        }
        else
        {
            if (g_selected.size() >= MAX_SELECTION)
                g_selected.erase(g_selected.begin());
            g_selected.push_back(hitIndex);
        }
    }
}

// ==================== 鼠标移动 ====================
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

// ==================== 鼠标按键（第 12 天新增）====================
// 同一个左键既要"拖拽旋转"又要"点击选中"。
// 办法：按下时记录位置，松开时看移动了多远——小就是点击。
static void mouse_button_callback(GLFWwindow* /*window*/, int button,
                                  int action, int mods)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    if (imguiWantsMouse())
        return;

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
            // 用当前相机参数重算一遍矩阵（回调不在渲染循环里）
            float camX = g_radius * std::cos(g_pitch) * std::sin(g_yaw);
            float camY = g_radius * std::sin(g_pitch);
            float camZ = g_radius * std::cos(g_pitch) * std::cos(g_yaw);
            glm::vec3 camPos(camX, camY, camZ);

            glm::mat4 view = glm::lookAt(camPos,
                                         glm::vec3(0.0f, 0.0f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 projection = glm::perspective(
                glm::radians(45.0f),
                static_cast<float>(g_winW) / static_cast<float>(g_winH),
                0.1f, 500.0f);

            bool additive = (mods & GLFW_MOD_SHIFT) != 0;
            doPick(g_mouseX, g_mouseY, additive, view, projection);
        }
    }
}

// ==================== 滚轮 ====================
static void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    if (imguiWantsMouse())
        return;

    float factor = 1.0f - (float)yoffset * 0.12f;
    g_radius *= factor;

    if (g_radius < g_radiusMin) g_radius = g_radiusMin;
    if (g_radius > g_radiusMax) g_radius = g_radiusMax;
}

// ==================== 键盘 ====================
static void key_callback(GLFWwindow* window, int key, int /*scancode*/,
                         int action, int /*mods*/)
{
    if (action != GLFW_PRESS)
        return;
    if (imguiWantsKeyboard())
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
    else if (key == GLFW_KEY_ESCAPE)
    {
        // Esc 优先取消选择；没有选中时才退出。
        // 这样误按 Esc 不会直接关掉窗口。
        if (!g_selected.empty())
            g_selected.clear();
        else
            glfwSetWindowShouldClose(window, true);
    }
}

// ==================== 窗口尺寸 ====================
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

// 以 mid 为顶点，a-mid-b 的夹角（度）
static float bondAngle(const Molecule& mol, int a, int mid, int b)
{
    glm::vec3 v1 = mol.atoms[a].position - mol.atoms[mid].position;
    glm::vec3 v2 = mol.atoms[b].position - mol.atoms[mid].position;

    float len1 = glm::length(v1);
    float len2 = glm::length(v2);
    if (len1 < 1e-6f || len2 < 1e-6f) return 0.0f;

    float c = glm::clamp(glm::dot(v1 / len1, v2 / len2), -1.0f, 1.0f);
    return glm::degrees(std::acos(c));
}

// ==================== 选择信息面板 ====================
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

        float d = glm::length(b.position - a.position);

        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
                           "%s%d  --  %s%d",
                           a.element.c_str(), ia + 1,
                           b.element.c_str(), ib + 1);
        ImGui::Text("Distance  %.3f A", d);

        // 若两者有共同邻居，顺便给出键角
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
static void DrawControlPanel(const std::string& fileName,
                             const Molecule& molecule,
                             const std::vector<std::string>& elements,
                             float molR)
{
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f),  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.90f);

    if (ImGui::Begin("Control Panel", &g_ui.showPanel))
    {
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "MOLECULE");
        ImGui::Separator();
        ImGui::Text("File   %s", fileName.c_str());
        ImGui::Text("Atoms  %d", static_cast<int>(molecule.atoms.size()));
        ImGui::Text("Bonds  %d", static_cast<int>(molecule.bonds.size()));
        ImGui::Text("Radius %.2f A", molR);
        ImGui::Spacing();

        // ---------- 选中信息（第 12 天）----------
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "SELECTION");
        ImGui::Separator();
        DrawSelectionInfo(molecule);
        ImGui::Spacing();

        // ---------- 显示 ----------
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

        // ---------- 图例 ----------
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
        ImGui::TextDisabled("Drag LMB  : rotate view");
        ImGui::TextDisabled("Click     : select atom");
        ImGui::TextDisabled("Shift+Clk : add 2nd atom");
        ImGui::TextDisabled("Scroll    : zoom");
        ImGui::TextDisabled("Space     : switch model");
        ImGui::TextDisabled("Tab       : show / hide panel");
        ImGui::TextDisabled("Esc       : clear selection / quit");
    }
    ImGui::End();
}

// ==================== 3D 标签 ====================
// 把原子世界坐标投影到屏幕，用 ImGui 前景绘制层写字。
// 必须在 NewFrame() 之后、Render() 之前调用。
static void DrawAtomLabels(const Molecule& molecule,
                           const glm::mat4& view,
                           const glm::mat4& projection)
{
    if (!g_ui.showSelectedLabels && !g_ui.showAllLabels)
        return;

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (fg == nullptr) return;

    for (int i = 0; i < static_cast<int>(molecule.atoms.size()); ++i)
    {
        bool sel = isSelected(i);

        if (!sel && !g_ui.showAllLabels)
            continue;

        glm::vec3 wp = atomWorldPos(molecule.atoms[i]);

        float sx = 0.0f, sy = 0.0f;
        if (!picking::worldToScreen(wp, view, projection,
                                    static_cast<float>(g_winW),
                                    static_cast<float>(g_winH),
                                    sx, sy))
            continue;

        // 简单裁剪：屏幕外的标签不画
        if (sx < -60.0f || sy < -60.0f ||
            sx > static_cast<float>(g_winW) + 60.0f ||
            sy > static_cast<float>(g_winH) + 60.0f)
            continue;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s%d",
                      molecule.atoms[i].element.c_str(), i + 1);

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

int main(int argc, char** argv)
{
    // ==================== 命令行参数 ====================
    std::string userPath = "assets/methane.mol";

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

    // ==================== 资源路径解析 ====================
    // 依次尝试：工作目录 → 可执行文件目录 → 可执行文件上级目录
    // 这样在项目根目录、build/ 目录、甚至别处用绝对路径调用，都能找到文件
    std::string molPath = pathutil::resolveResource(userPath);
    if (molPath.empty())
    {
        std::cerr << "[错误] 找不到分子文件: " << userPath << "\n"
                  << "       当前工作目录: " << std::filesystem::current_path() << "\n"
                  << "       试试: ./molecule_viewer assets/methane.mol\n";
        return -1;
    }

    std::string fileName = molPath;
    {
        size_t slash = fileName.find_last_of("/\\");
        if (slash != std::string::npos)
            fileName = fileName.substr(slash + 1);
    }

    // ==================== GLFW ====================
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
                                          "Molecule Viewer - Day 12", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建窗口失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // 先注册我们的回调，ImGui 初始化时会包装它们
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "[错误] GLAD 初始化失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "Molecule Viewer v" << APP_VERSION << std::endl;
    std::cout << "OpenGL: " << glGetString(GL_VERSION)
              << " | " << glGetString(GL_RENDERER) << std::endl;

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ==================== ImGui ====================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ==================== 加载分子 ====================
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

    g_molecule = &molecule;

    std::cout << "已加载: " << molPath << std::endl;
    std::cout << "  原子数: " << molecule.atoms.size()
              << "，键数: " << molecule.bonds.size() << std::endl;

    std::vector<std::string> elements;
    {
        std::map<std::string, int> counts;
        for (const auto& a : molecule.atoms)
            counts[a.element]++;

        std::cout << "  元素组成: ";
        for (const auto& kv : counts)
        {
            std::cout << kv.first << kv.second << " ";
            elements.push_back(kv.first);
        }
        std::cout << std::endl;
    }

    // ==================== 自动相机 ====================
    float molR = molfile::moleculeRadius(molecule);
    if (molR < 0.1f) molR = 1.0f;

    const float FOV_DEG = 45.0f;
    float dist = molR / std::tan(glm::radians(FOV_DEG) * 0.5f) * 1.55f;

    g_radiusMin = dist * 0.12f;
    g_radiusMax = dist * 5.0f;
    g_radius    = dist;
    g_lastFrame = (float)glfwGetTime();

    glfwGetWindowSize(window, &g_winW, &g_winH);

    std::cout << "  分子半径: " << molR
              << "，相机初始距离: " << dist << std::endl;
    std::cout << "操作: 左键拖拽旋转 | 单击选中原子 | Shift+单击选第二个"
              << " | 空格切换模型 | Tab 面板 | Esc 取消选择/退出" << std::endl;

    // ==================== 网格 ====================
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

    // ==================== 着色器 ====================
    // 着色器同样走路径解析，这样从 build/ 里运行也能找到
    std::string svPath = pathutil::resolveResource("src/shaders/sphere.vert");
    std::string sfPath = pathutil::resolveResource("src/shaders/sphere.frag");
    std::string cvPath = pathutil::resolveResource("src/shaders/cylinder.vert");
    std::string cfPath = pathutil::resolveResource("src/shaders/cylinder.frag");

    if (svPath.empty() || sfPath.empty() || cvPath.empty() || cfPath.empty())
    {
        std::cerr << "[错误] 找不到着色器文件。\n"
                  << "       请确认 src/shaders/ 目录存在。\n"
                  << "       当前工作目录: " << std::filesystem::current_path() << "\n";

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    Shader sphereShader(svPath.c_str(), sfPath.c_str());
    Shader cylinderShader(cvPath.c_str(), cfPath.c_str());

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

    std::vector<std::pair<float, int>> drawOrder;

    // ==================== 渲染循环 ====================
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        float deltaTime = currentFrame - g_lastFrame;
        g_lastFrame = currentFrame;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        glfwPollEvents();
        glfwGetWindowSize(window, &g_winW, &g_winH);

        if (g_ui.autoRotate)
            g_autoAngle += g_ui.rotateSpeed * deltaTime;

        // 更新旋转矩阵（渲染、拾取、标签共用同一份）
        g_spinMatrix = glm::mat4(1.0f);
        if (g_autoAngle != 0.0f)
            g_spinMatrix = glm::rotate(glm::mat4(1.0f), g_autoAngle,
                                       glm::vec3(0.0f, 1.0f, 0.0f));

        // ---------- 相机与矩阵（UI 阶段就要用，提前算）----------
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

        float lightScale = std::max(1.0f, molR);
        glm::vec3 lightPos     = g_cameraPos + KEY_LIGHT_OFFSET  * lightScale;
        glm::vec3 fillLightPos = g_cameraPos + FILL_LIGHT_OFFSET * lightScale;
        glm::vec3 fillColor    = FILL_LIGHT_BASE * g_ui.fillStrength;

        // ---------- ImGui ----------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (g_ui.showPanel)
            DrawControlPanel(fileName, molecule, elements, molR);

        DrawAtomLabels(molecule, view, projection);

        ImGui::Render();

        // ---------- 场景 ----------
        glClearColor(g_ui.bgColor[0], g_ui.bgColor[1], g_ui.bgColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        drawOrder.clear();
        for (int i = 0; i < (int)molecule.atoms.size(); ++i)
        {
            float d = glm::length(atomWorldPos(molecule.atoms[i]) - g_cameraPos);
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
        glUniform1f(s_ambient,  g_ui.ambientStrength);
        glUniform1f(s_specular, g_ui.specularStrength);
        glUniform1f(s_shiny,    g_ui.shininess);
        glUniform1f(s_normalOff, (g_ui.renderMode == 1) ? -0.002f : 0.0f);

        glBindVertexArray(sphereVAO);

        for (const auto& pair : drawOrder)
        {
            int idx = pair.second;
            const Atom& atom = molecule.atoms[idx];

            float r = displayRadius(atom);

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, atom.position);
            model = glm::scale(model, glm::vec3(r));
            model = g_spinMatrix * model;

            glm::mat3 normalMatrix =
                glm::transpose(glm::inverse(glm::mat3(model)));

            glm::vec3 color = getElementStyle(atom.element).color;
            if (isSelected(idx))
                color = glm::mix(color, HIGHLIGHT_COLOR, 0.60f);

            glUniformMatrix4fv(s_model, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(s_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniform3f(s_objColor, color.r, color.g, color.b);

            glDrawElements(GL_TRIANGLES, (GLsizei)sphereIdx.size(),
                           GL_UNSIGNED_INT, 0);
        }

        // ---------- 画键 ----------
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

            glBindVertexArray(cylVAO);

            for (const auto& bond : molecule.bonds)
            {
                glm::vec3 a = molecule.atoms[bond.a].position;
                glm::vec3 b = molecule.atoms[bond.b].position;

                bool touched = isSelected(bond.a) || isSelected(bond.b);
                glm::vec3 bc = touched
                             ? glm::mix(BOND_COLOR, HIGHLIGHT_COLOR, 0.45f)
                             : BOND_COLOR;
                glUniform3f(c_objColor, bc.r, bc.g, bc.b);

                glm::mat4 model = makeBondModel(a, b);
                model = glm::scale(model, glm::vec3(BOND_RADIUS, 1.0f, BOND_RADIUS));
                model = g_spinMatrix * model;

                glm::mat3 normalMatrix =
                    glm::transpose(glm::inverse(glm::mat3(model)));

                glUniformMatrix4fv(c_model, 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix3fv(c_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));

                glDrawElements(GL_TRIANGLES, (GLsizei)cylIdx.size(),
                               GL_UNSIGNED_INT, 0);
            }
        }

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ==================== 清理 ====================
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    glDeleteVertexArrays(1, &cylVAO);
    glDeleteBuffers(1, &cylVBO);
    glDeleteBuffers(1, &cylEBO);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
