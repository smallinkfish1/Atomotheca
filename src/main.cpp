// src/main.cpp
// 第 20 天：电子云点云（Atomotheca 模块第 6 天）。
//
// 主要改动：
//   1. 新增 orbital/cloud.{h,cpp}：径向函数 R_nl + 实球谐 Y（带符号）→ |ψ|²，
//      先反演径向 CDF 抽 r、再按 |Y|² 拒绝采样抽方向，撒成千上万个点
//   2. 新增 cloud.vert / cloud.frag：GL_POINTS + 圆盘软边 + 相位两色
//   3. 渲染新增第三个阶段「点云」：在不透明之后、轨道形状之前
//   4. 新增 ui/cloud_panel.{h,cpp}：n / 角向 / 点数 / 点径 / 不透明度
//   5. 新增 --dump-cloud，用统计量和 ASCII 剖面核对采样是否正确（不开窗口）
//
// 第 19 天（沿用）：3D d 轨道（角向形状）。
// 第 18 天（沿用）：s / p 轨道形状 + 半透明两阶段渲染。
// 第 17 天（沿用）：2D 能级图。
// 第 16 天（沿用）：Atomotheca 元素周期表面板。
// 第 15 天（沿用）：Atomotheca 数据层——元素数据库（JSON 驱动）。
// 第 14 天（沿用）：Emscripten / WebAssembly 适配。
//
// 桌面与 Web 的功能完全一致：球棍/填充、光照、拾取、键长键角、元素面板、轨道形状、电子云。

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
#include "orbital/element.h" // Atomotheca 数据层（不依赖 OpenGL）
#include "orbital/shape.h"   // 第 18 天：轨道形状网格（同上，纯数学）
#include "orbital/cloud.h"   // 第 20 天：电子云采样（同上，纯数学）
#include "ui/periodic_table.h"  // 第 16 天：元素周期表面板（只依赖 ImGui + 数据层）
#include "ui/energy_diagram.h"  // 第 17 天：能级图（同上，只依赖 ImGui + 数据层）
#include "ui/orbital_panel.h"   // 第 18 天：轨道形状控制项（同上）
#include "ui/cloud_panel.h"     // 第 20 天：电子云控制项（同上）

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
#  define APP_VERSION "0.2.2-dev"
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
    Shader* orbitalShader  = nullptr;   // 第 18 天：半透明轨道
    Shader* cloudShader    = nullptr;   // 第 20 天：电子云点云

    unsigned int sphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
    unsigned int cylVAO    = 0, cylVBO    = 0, cylEBO    = 0;

    GLsizei sphereIndexCount = 0;
    GLsizei cylIndexCount    = 0;

    // ---------- 第 18 天：轨道形状的 GPU 资源（第 19 天扩到 9 个）----------
    // 每个形状一份 VBO/EBO。看着比"一个网格 + 旋转矩阵"浪费，
    // 但对称轴直接烘进顶点里，渲染时不用再维护一堆旋转矩阵，
    // 也避免了"p_y 的模型矩阵写错 90°"这类只有肉眼能发现的 bug。
    unsigned int orbVAO[orbital::SHAPE_COUNT] = {};
    unsigned int orbVBO[orbital::SHAPE_COUNT] = {};
    unsigned int orbEBO[orbital::SHAPE_COUNT] = {};

    // 每瓣在 EBO 里的区间。第 18 天 s / p 每瓣索引数相同，用「总长 ÷ 瓣数」够用；
    // d 的腰环和两极瓣三角形数差很多（见 --dump-orbital 的 tris/lobe），
    // 必须逐瓣记账，否则偏移量会全部错位。
    std::vector<int> orbLobeFirst[orbital::SHAPE_COUNT];
    std::vector<int> orbLobeSize[orbital::SHAPE_COUNT];

    // 瓣心（单位尺度，3 个 float 一组）。渲染时乘上 model 得到世界坐标，
    // 按到相机的距离从远到近排序 —— 半透明不排序就会前后乱穿。
    std::vector<float> orbLobeCenter[orbital::SHAPE_COUNT];

    // orbital uniforms
    GLint o_model = -1, o_view = -1, o_proj = -1, o_normalMat = -1;
    GLint o_color = -1, o_alpha = -1, o_viewPos = -1, o_lightPos = -1;

    // ---------- 第 20 天：电子云的 GPU 资源 ----------
    // 只有一份点云：换轨道就整批重传。点云没有拓扑关系，重建成本就是一次
    // glBufferData（6 万个点 × 16 字节 ≈ 1 MB），比维护九份缓存简单得多。
    unsigned int cloudVAO = 0, cloudVBO = 0;
    GLsizei      cloudPointCount = 0;

    // 已经上传的是哪一套量子数 —— 和面板请求的不一致时才重新采样。
    // 这是「渲染循环内禁止动态分配」的落地方式：抽样会分配几十万个点，
    // 绝不能每帧做。
    int                cloudBuiltN          = 0;
    orbital::ShapeKind cloudBuiltKind       = orbital::ShapeKind::S;
    int                cloudBuiltPointCount = 0;
    unsigned           cloudBuiltSeed       = 0;

    orbital::CloudStats cloudStats;     // 面板显示 mean r / r98，渲染按 r98 归一缩放

    // cloud uniforms
    GLint cl_model = -1, cl_view = -1, cl_proj = -1, cl_viewPos = -1;
    GLint cl_sizeWorld = -1, cl_sizeScale = -1;
    GLint cl_colorPos = -1, cl_colorNeg = -1, cl_opacity = -1;

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

// ==================== Atomotheca 元素数据库 ====================
// 第 15 天：化学数据全部来自 assets/data/elements.json，C++ 里不硬编码。
// 数据层加载失败不影响分子查看器的正常使用，只是周期表面板暂时不可用。
orbital::ElementDatabase g_elements;

// 加载元素数据库。路径走三级回退（CWD → exe 目录 → exe 上级），
// 和分子文件、着色器用同一套解析逻辑。成功返回 true。
static bool loadElementDatabase(std::string* error)
{
    const std::string path = pathutil::resolveResource("assets/data/elements.json");
    if (path.empty())
    {
        *error = "找不到 assets/data/elements.json"
                 "（已尝试：工作目录 / 可执行文件目录 / 可执行文件上级目录）";
        return false;
    }
    return g_elements.loadFromFile(path, error);
}

// 按符号查元素，容错「大小写写错」（fe → Fe）。
// 不做完整的忽略大小写比较：元素符号只有 1–2 个字母，首字母大写 + 其余小写就够了。
static const orbital::Element* findElementLoose(const std::string& symbol)
{
    const orbital::Element* element = g_elements.find(symbol);
    if (element != nullptr) return element;

    std::string fixed = symbol;
    if (!fixed.empty())
    {
        for (char& c : fixed)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        fixed[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(fixed[0])));
    }
    return g_elements.find(fixed);
}

// ==================== 周期表面板状态 ====================
// 面板自己的状态（看哪个元素、哪个离子、开不开）归面板，
// 不塞进 UiState —— 那里放的是分子显示相关的东西，混在一起会让拆分更难。
ui::PeriodicTableState g_periodic;

// ==================== 能级图状态 ====================
// 能级图不自己记「看哪个元素」：那是 g_periodic.selectedZ 的职责。
// 它只持有显示开关和一份「上次按哪个元素算的」缓存，避免每帧重建能级行。
// 两个面板共用一个选择，比各自维护一份再想办法同步要可靠。
ui::EnergyDiagramState g_energy;

// ==================== 轨道形状状态 ====================
// 这份状态和 g_periodic 没有耦合：轨道形状是「看哪一种几何体」，
// 和「看哪个元素」是两件事（Day 19 加了 d 轨道之后也仍然无关）。
ui::OrbitalState g_orbital;

// ==================== 电子云状态 ====================
// 和 g_orbital 并列但互不影响：一个是「形状外壳」（|Y|），
// 一个是「概率点云」（|ψ|²），可以同时开，也可以各看各的。
ui::CloudState g_cloud;

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

// 第 18 天：轨道形状的整体尺寸 = 分子半径 × 这个系数。
// 取 1.3 是为了让 p 的两瓣伸出分子外壳一点 —— 正好包住就看不清"哑铃"了。
const float ORBITAL_EXTENT_FACTOR = 1.30f;

// 第 20 天：电子云的半径 = 分子半径 × 这个系数。
// 比轨道形状大一点：点云没有硬轮廓，靠外圈零散的点才看出"云"，
// 收得太紧会显得像一团实心球。真实半径（r98）在面板上以 a₀ 显示。
const float CLOUD_RADIUS_FACTOR = 1.70f;

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
// 原子现在被两处「看轨道」的功能临时隐藏（形状外壳 / 电子云），
// 判断条件必须只有一份：三处各写一遍，早晚会出现「画面上没有原子、
// 却能选中它」这种前后不一致的状态。
static bool moleculeHiddenForOrbitals()
{
    return (g_orbital.visible && g_orbital.hideMolecule) ||
           (g_cloud.visible   && g_cloud.hideMolecule);
}

static void doPick(float sx, float sy, bool additive,
                   const glm::mat4& view, const glm::mat4& projection)
{
    if (g_app.molecule.atoms.empty()) return;

    // 第 18 天：分子被隐藏时（只看轨道形状 / 电子云）不参与拾取。
    // 否则会出现"点了空处却选中一个看不见的原子、右侧面板冒出原子信息"的怪状态。
    if (moleculeHiddenForOrbitals()) return;

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
    else if (key == GLFW_KEY_P)
    {
        // 第 16 天：周期表是独立窗口，单独一个开关，不和 Tab 绑在一起
        g_periodic.visible = !g_periodic.visible;
    }
    else if (key == GLFW_KEY_L)
    {
        // 第 17 天：能级图同样独立开关
        g_energy.visible = !g_energy.visible;
    }
    else if (key == GLFW_KEY_O)
    {
        // 第 18 天：3D 轨道形状。Ctrl+O 是打开文件，在上面已经 return 了，
        // 所以这里只可能是单独按 O。
        g_orbital.visible = !g_orbital.visible;
    }
    else if (key == GLFW_KEY_C)
    {
        // 第 20 天：电子云。和 O 并列，可以同时开（云 + 外壳一起看）
        g_cloud.visible = !g_cloud.visible;
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

        // 第 16–17 天：周期表与能级图开关。
        // 数据库没加载成功时禁用，而不是让用户点开一个空窗口。
        if (g_elements.empty())
        {
            ImGui::BeginDisabled();
            ImGui::Checkbox("Periodic table  (P)", &g_periodic.visible);
            ImGui::Checkbox("Energy levels   (L)", &g_energy.visible);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("no data");
        }
        else
        {
            ImGui::Checkbox("Periodic table  (P)", &g_periodic.visible);
            ImGui::Checkbox("Energy levels   (L)", &g_energy.visible);
        }
        ImGui::Spacing();

        // 第 18 天：轨道形状。放在控制面板里而不是新开窗口——
        // 屏幕上已经有面板 + 周期表 + 能级图，再加窗口只会互相遮挡。
        ui::drawOrbitalControls(g_orbital);
        ImGui::Spacing();

        // 第 20 天：电子云。同样是一段折叠区，和轨道形状各管各的。
        ui::drawCloudControls(g_cloud, g_app.cloudStats);
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
        ImGui::TextDisabled("P         : periodic table");
        ImGui::TextDisabled("L         : energy levels");
        ImGui::TextDisabled("O         : orbital shape (s / p / d)");
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
//  不透明阶段：原子与键
//
//  第 18 天从 renderFrame 里原样抽出来的，不是新功能。
//  抽出来的唯一理由是：轨道是半透明的，而透明物体必须
//  「等所有不透明物体画完」再画。如果两个阶段继续挤在同一个函数里、
//  只靠缩进区分，以后加新的透明物体时迟早会插到原子前面去。
// ============================================================
static void drawMoleculeOpaque(const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& lightPos, const glm::vec3& fillLightPos,
                               const glm::vec3& fillColor)
{
    AppState& app = g_app;

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
}

// ============================================================
//  透明阶段：s / p 轨道形状
//
//  半透明渲染有三个必须同时满足的约束，缺一条就会"前后乱穿"：
//    1. 画在不透明物体**之后**。否则轨道被原子遮住的部分已经写进了颜色缓冲，
//       之后原子再画一遍反而会把轨道盖掉。
//    2. 从远到近。alpha 混合不可交换：A 叠 B 和 B 叠 A 的结果不同。
//    3. 关掉深度写入（glDepthMask(GL_FALSE)）。深度**测试**要留着
//       （这样被原子挡住的部分仍然不画），但深度值不更新，
//       否则先画的瓣会把后画的瓣整块剔除掉。
//
//  每一瓣内部再分两次画（先背面后正面），半透明的球才有"能看进内部"的层次感；
//  只画正面会变成一个扁平的色块。
// ============================================================
static void drawOrbitals(const glm::mat4& view, const glm::mat4& projection,
                         const glm::vec3& lightPos)
{
    AppState& app = g_app;
    const int kind = static_cast<int>(g_orbital.kind);

    if (app.orbitalShader == nullptr || app.orbLobeCenter[kind].empty())
        return;

    // 轨道尺寸跟着分子走：换分子时视觉比例不变。
    const float extent = std::max(app.molR, 0.35f) * ORBITAL_EXTENT_FACTOR;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::scale(model, glm::vec3(extent));
    model = g_spinMatrix * model;      // 跟着自动旋转一起转，否则会"漂"在分子外面

    // 瓣心（单位尺度）变换到世界坐标，按到相机的距离从远到近排序。
    // 瓣数最多 6 个（d_z2 是两极 + 腰环），冒泡足够 ——
    // 而且渲染循环里不许动态分配，所以用固定长度数组而不是 std::vector。
    struct LobeSort
    {
        float dist;
        int   index;
    };
    LobeSort lobes[8];
    int      count = 0;

    const std::vector<float>& centers = app.orbLobeCenter[kind];
    for (std::size_t i = 0; i + 2 < centers.size() && count < 8; i += 3)
    {
        const glm::vec4 w = model * glm::vec4(centers[i], centers[i + 1], centers[i + 2], 1.0f);
        lobes[count].dist  = glm::length(glm::vec3(w) - g_cameraPos);
        lobes[count].index = static_cast<int>(i / 3);
        ++count;
    }

    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
            if (lobes[j].dist > lobes[i].dist)
                std::swap(lobes[i], lobes[j]);

    const orbital::Rgb rgb = orbital::shapeColor(g_orbital.kind);
    const glm::mat3    normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

    app.orbitalShader->use();
    glUniformMatrix4fv(app.o_view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(app.o_proj, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(app.o_model, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix3fv(app.o_normalMat, 1, GL_FALSE, glm::value_ptr(normalMatrix));
    glUniform3f(app.o_color, rgb.r, rgb.g, rgb.b);
    glUniform1f(app.o_alpha, g_orbital.alpha);
    glUniform3f(app.o_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);
    glUniform3f(app.o_lightPos, lightPos.x, lightPos.y, lightPos.z);

    glBindVertexArray(app.orbVAO[kind]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < count; ++i)
    {
        // 每瓣的索引在 EBO 里是连续一段，起点和长度都在建网格时记好了
        const int   lobe  = lobes[i].index;
        const void* offset = reinterpret_cast<const void*>(
            static_cast<std::size_t>(app.orbLobeFirst[kind][lobe]) * sizeof(unsigned int));
        const GLsizei lobeIndexCount = static_cast<GLsizei>(app.orbLobeSize[kind][lobe]);

        // 远的那半张球面先落缓冲：剔除正面，只留背面
        glCullFace(GL_FRONT);
        glDrawElements(GL_TRIANGLES, lobeIndexCount, GL_UNSIGNED_INT, offset);

        // 再画朝着相机的正面
        glCullFace(GL_BACK);
        glDrawElements(GL_TRIANGLES, lobeIndexCount, GL_UNSIGNED_INT, offset);
    }

    // 状态必须还原。忘了 glDepthMask(GL_TRUE) 的后果特别隐蔽：
    // 下一帧的原子写不进深度缓冲，整个场景的遮挡关系会时好时坏。
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);
    glBindVertexArray(0);
}

// ============================================================
//  第 20 天：电子云点云
//
//  和最上面那段三角形渲染的差别，全在「没有三角形」这件事上：
//
//  1. 一个点就是一个像素方块（gl_PointSize 可以改大），片段着色器里
//     用 gl_PointCoord 把它裁成圆盘。没有顶点、没有绕序、没有背面。
//  2. 点的**数量密度**就是概率密度，所以每个点的 alpha 是常数。
//     再按 ρ 调亮度等于把密度平方一次，中心会糊成死白（见 cloud.cpp 开头）。
//  3. 几十万个点没法排序（半透明排序那一套在这里失效），
//     所以只做「深度测试 + 不写深度」：被原子挡住的不画，
//     点与点之间按到达顺序混合。因为单点 alpha 很低、点又小，
//     顺序差异肉眼看不出来 —— 这是点云和三角面片在混合策略上的根本区别。
//  4. 尺寸按 r98 归一：不同 n 的真实半径差好几倍（1s ≈ 3 a₀、4s ≈ 30 a₀），
//     统一缩放到分子尺度，比的才是「形状」而不是「大小」。真实半径写在面板上。
// ============================================================
static void drawCloud(const glm::mat4& view, const glm::mat4& projection)
{
    AppState& app = g_app;

    if (app.cloudShader == nullptr || app.cloudPointCount <= 0 || app.cloudStats.r98 <= 0.0f)
        return;

    const float radiusWorld = std::max(app.molR, 0.35f) * CLOUD_RADIUS_FACTOR;
    const float scale       = radiusWorld / app.cloudStats.r98;

    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    model = g_spinMatrix * model;      // 和轨道形状一样跟着自动旋转，否则会"漂"在外面

    // 点的世界直径 = 云半径 × 面板上的百分比
    const float pointSizeWorld = radiusWorld * g_cloud.pointSize * 0.01f;

    // 世界尺寸 → 像素：距离 d 处，1 个世界单位占 (视口高/2)/(tan(fov/2)·d) 个像素。
    // 视口高要用**帧缓冲**高度而不是窗口高度：HiDPI 屏上两者差一倍，
    // 用窗口高度会让 Retina 上点小一半。
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(app.window, &fbW, &fbH);
    const float sizeScale =
        (fbH > 0) ? (0.5f * static_cast<float>(fbH)) /
                    std::tan(glm::radians(FOV_DEG) * 0.5f)
                  : 600.0f;

    const orbital::Rgb positive = orbital::phaseColor(+1.0f);
    const orbital::Rgb negative = orbital::phaseColor(-1.0f);

    app.cloudShader->use();
    glUniformMatrix4fv(app.cl_model, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(app.cl_view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(app.cl_proj, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3f(app.cl_viewPos, g_cameraPos.x, g_cameraPos.y, g_cameraPos.z);
    glUniform1f(app.cl_sizeWorld, pointSizeWorld);
    glUniform1f(app.cl_sizeScale, sizeScale);
    glUniform3f(app.cl_colorPos, positive.r, positive.g, positive.b);
    glUniform3f(app.cl_colorNeg, negative.r, negative.g, negative.b);
    glUniform1f(app.cl_opacity, g_cloud.opacity);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);             // 测试留着、写入关掉：点云不能互相遮挡

    glBindVertexArray(app.cloudVAO);
    glDrawArrays(GL_POINTS, 0, app.cloudPointCount);
    glBindVertexArray(0);

    // 还原因透明阶段借用过的状态（漏掉 glDepthMask 的后果见 drawOrbitals 末尾）
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ============================================================
//  一帧的全部工作
//
//  桌面：由 while 循环反复调用
//  Web ：由 emscripten_set_main_loop 反复调用
//
//  所以它不能有任何"局部持久状态"——所有跨帧数据都在 g_app / 全局变量里
// ============================================================
// 前置声明：函数体在 setupOrbitalMeshes 之后（那里才是「建网格」的地盘），
// 但渲染循环需要在参数变化时调用它。
static void setupCloudPoints();

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

    // 第 16 天：周期表是独立窗口，跟控制面板的显隐互不影响
    ui::drawPeriodicTable(g_elements, g_periodic);

    // 第 17 天：能级图跟着周期表的选择走（同一个元素、同一个离子）。
    // 把「看什么」只存在 g_periodic 一处，两个面板就不会各说各话。
    if (g_energy.visible && !g_elements.empty())
    {
        if (const orbital::Element* current = g_elements.findByZ(g_periodic.selectedZ))
            ui::drawEnergyDiagram(g_elements, current->atomicNumber,
                                  g_periodic.selectedCharge(*current), g_energy);
    }

    // 第 20 天：电子云只在参数变化时重采样。
    // 比的是四个标量（n / 角向 / 点数 / 种子），不是比整块点坐标 ——
    // 几十万个 float 的逐字节比较每帧都要跑一遍，比重新采样还慢。
    if (g_cloud.visible &&
        (g_cloud.n != app.cloudBuiltN ||
         g_cloud.angular != app.cloudBuiltKind ||
         g_cloud.pointCount != app.cloudBuiltPointCount ||
         g_cloud.seed != app.cloudBuiltSeed))
    {
        setupCloudPoints();
    }

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

    // 第 18 天：分子被隐藏时，标签也不能留 —— 否则几个字母会浮在轨道形状上，
    // 指向根本不存在的原子。（第 20 天的电子云用同一个判断）
    const bool moleculeVisible = !moleculeHiddenForOrbitals();

    if (moleculeVisible)
        DrawAtomLabels(view, projection);

    ImGui::Render();

    // ---------- 场景 ----------
    glClearColor(g_ui.bgColor[0], g_ui.bgColor[1], g_ui.bgColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 分三个阶段：先不透明（原子、键），再点云，最后半透明外壳。
    // 顺序不能颠倒 —— 透明物体必须最后画，否则会被先画的原子盖掉。
    // 点云排在轨道形状之前：外壳是「包在外面的壳」，让壳压在云上面更符合直觉。
    if (moleculeVisible)
        drawMoleculeOpaque(view, projection, lightPos, fillLightPos, fillColor);

    // 第 20 天：电子云（几十万个半透明点）
    if (g_cloud.visible)
        drawCloud(view, projection);

    // 第 18 天：轨道形状（半透明，必须在不透明阶段之后）
    if (g_orbital.visible)
        drawOrbitals(view, projection, lightPos);

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

// ==================== 第 18 天：轨道形状网格（第 19 天扩到 9 个）====================
// 9 个形状在启动时一次建好。几何是 orbital/shape.cpp 算出来的纯数学产物，
// 这里只做两件事：上传显存、记下每瓣的索引区间（半透明排序时要分瓣画）。
static void setupOrbitalMeshes()
{
    AppState& app = g_app;

    for (int i = 0; i < orbital::SHAPE_COUNT; ++i)
    {
        const orbital::ShapeKind kind = static_cast<orbital::ShapeKind>(i);

        // 统一按单位尺度生成，实际大小由 model 矩阵缩放 ——
        // 这样面板里调尺寸不用重建 VBO。
        orbital::ShapeMesh mesh;
        orbital::buildShape(kind, 1.0f, mesh);

        app.orbLobeCenter[i] = mesh.lobeCenters;
        app.orbLobeFirst[i]  = mesh.lobeFirst;
        app.orbLobeSize[i]   = mesh.lobeSize;

        glGenVertexArrays(1, &app.orbVAO[i]);
        glGenBuffers(1, &app.orbVBO[i]);
        glGenBuffers(1, &app.orbEBO[i]);

        glBindVertexArray(app.orbVAO[i]);

        glBindBuffer(GL_ARRAY_BUFFER, app.orbVBO[i]);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                     mesh.vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, app.orbEBO[i]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
                     mesh.indices.data(), GL_STATIC_DRAW);

        // 顶点布局和原子球完全一致：[pos xyz][normal xyz] 各 6 个 float
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }
}

// ============================================================
//  第 20 天：电子云的点缓冲
//
//  面板一次只请求一套量子数，所以只维护一份 VBO。
//  比对「已经上传的那一套」和「面板现在要的那一套」，不一致才重新采样 ——
//  采 6 万个点要几十毫秒，还要分配 1 MB 内存，放进每帧必跑的主循环是灾难。
//  所以这里不是「每帧重建」，而是「参数变了才重建」。
// ============================================================
static void setupCloudPoints()
{
    AppState& app = g_app;

    if (app.cloudVAO == 0)
    {
        glGenVertexArrays(1, &app.cloudVAO);
        glGenBuffers(1, &app.cloudVBO);
    }

    std::vector<orbital::CloudPoint> points;
    orbital::CloudStats              stats;
    orbital::buildCloud(g_cloud.n, g_cloud.angular, g_cloud.pointCount,
                        g_cloud.seed, points, stats);

    app.cloudStats      = stats;
    app.cloudPointCount = static_cast<GLsizei>(points.size());

    glBindVertexArray(app.cloudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, app.cloudVBO);

    // GL_DYNAMIC_DRAW：换轨道就整块替换，不是一次上传终身不变
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(points.size() * sizeof(orbital::CloudPoint)),
                 points.empty() ? nullptr : points.data(),
                 GL_DYNAMIC_DRAW);

    // 顶点布局：[x, y, z, phase]，4 个 float 一组。
    // 和球/轨道网格的 [pos, normal] 不同，所以 stride 与偏移量都是自己的。
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    app.cloudBuiltN          = g_cloud.n;
    app.cloudBuiltKind       = g_cloud.angular;
    app.cloudBuiltPointCount = g_cloud.pointCount;
    app.cloudBuiltSeed       = g_cloud.seed;
}

// ==================== GLFW 错误回调 ====================
// GLFW 的 API 大多是"失败只返回 0/false"，不抛异常、不打日志。
// 不挂这个回调，出错时只能看到一句"初始化失败"，排查全靠猜。
static void glfwErrorCallback(int code, const char* description)
{
    std::cerr << "[GLFW 错误 " << code << "] " << description << std::endl;
}

// ==================== main ====================
int main(int argc, char** argv)
{
    // ---------- 命令行（Web 上没有）----------
    std::string userPath = "assets/methane.mol";

#ifndef __EMSCRIPTEN__
    std::string dumpSymbol;   // --dump 的目标元素符号，空表示不 dump
    bool        dumpTable = false;   // --dump-table：打印 ASCII 周期表
    std::string dumpLevelsSymbol;    // --dump-levels 的目标元素符号
    bool        dumpOrbital    = false;   // 是否出现 --dump-orbital
    bool        dumpOrbitalAll = false;   // 不带形状参数 = 九个形状全打
    orbital::ShapeKind dumpOrbitalKind = orbital::ShapeKind::S;

    bool        dumpCloud     = false;    // 是否出现 --dump-cloud
    bool        dumpCloudSpec = false;    // 是否给了具体的 n + 形状
    std::string dumpCloudBad;             // 形如 "2d" / "7s"：想写轨道，但这组合不存在
    int         dumpCloudN    = 1;
    orbital::ShapeKind dumpCloudKind = orbital::ShapeKind::S;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            std::cout
                << "Molecule Viewer v" << APP_VERSION << "\n\n"
                << "用法: molecule_viewer [分子文件] [--dump <元素符号>]\n"
                << "                      [--dump-table] [--dump-levels <元素符号>]\n"
                << "                      [--dump-orbital [形状]]\n"
                << "                      [--dump-cloud [n+形状]]\n\n"
                << "  [分子文件]   支持 .mol (V2000) 和 .pdb\n"
                << "               省略时默认加载 assets/methane.mol\n\n"
                << "  --dump <符号>  打印元素的电子排布与所有常见离子，然后退出。\n"
                << "                 用来单独验证数据层，不打开窗口。\n\n"
                << "                 例: --dump O   --dump Fe\n\n"
                << "  --dump-table   以 ASCII 打印周期表（18 族 × N 周期），然后退出。\n"
                << "                 用来核对「按 period/group 定位」的排布是否正确，\n"
                << "                 同样不需要显卡。'.' 表示真实存在但数据库没收录。\n\n"
                << "  --dump-levels <符号> 以 ASCII 打印能级图（中性原子 + 所有常见离子）。\n"
                << "                 从上到下是能量由高到低，所以 4s 会出现在 3d 下面。\n"
                << "                 例: --dump-levels Fe\n\n"
                << "  --dump-orbital [形状]  打印轨道网格的自检结果与 ASCII 剖面。\n"
                << "                 形状可取 s / px / py / pz /\n"
                << "                          dxy / dyz / dxz / dx2y2 / dz2，省略则九个全打。\n"
                << "                 检查顶点数、包围盒、法线长度、三角形绕序、\n"
                << "                 顶点是否落在解析曲面上、有向体积是否对得上。\n"
                << "                 例: --dump-orbital   --dump-orbital dz2\n\n"
                << "  --dump-cloud [n形状]  打印电子云采样的自检结果与 ASCII 剖面。\n"
                << "                 写法是「主量子数 + 形状」：1s / 2pz / 3dz2 / 4dxy，\n"
                << "                 省略则打一组默认轨道的摘要表。\n"
                << "                 检查 <r> 与 <r2> 是否对上闭式解、<cos2> 是否对上球面积分、\n"
                << "                 径向节面数是否等于 n-l-1、径向 CDF 的 KS 统计量。\n"
                << "                 例: --dump-cloud   --dump-cloud 3dz2\n\n"
                << "操作:\n"
                << "  左键拖拽     旋转视角\n"
                << "  左键单击     选中原子\n"
                << "  Shift+单击   追加选中第二个原子\n"
                << "  滚轮         缩放\n"
                << "  空格         切换 球棍 / 空间填充 模型\n"
                << "  Tab          显示 / 隐藏控制面板\n"
                << "  P            显示 / 隐藏元素周期表\n"
                << "  L            显示 / 隐藏能级图\n"
                << "  O            显示 / 隐藏轨道形状（s / p / d）\n"
                << "  C            显示 / 隐藏电子云（概率密度点云）\n"
                << "  Esc          取消选择；没有选中时退出\n";
            return 0;
        }
        if (arg == "-v" || arg == "--version")
        {
            std::cout << "Molecule Viewer v" << APP_VERSION << "\n";
            return 0;
        }

        // --dump 排他执行：打印完就退出，不初始化 GLFW，也不需要分子文件
        if (arg == "--dump")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "[错误] --dump 后面要跟元素符号，例如: --dump Fe\n";
                return -1;
            }
            dumpSymbol = argv[++i];
            continue;
        }

        // --dump-table 和 --dump 一样是排他执行：打印完就退出
        if (arg == "--dump-table")
        {
            dumpTable = true;
            continue;
        }

        // 第 17 天：--dump-levels 也是排他执行
        if (arg == "--dump-levels")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "[错误] --dump-levels 后面要跟元素符号，例如: --dump-levels Fe\n";
                return -1;
            }
            dumpLevelsSymbol = argv[++i];
            continue;
        }

        // 第 18 天：--dump-orbital [形状]
        // 形状参数是可选的，而「可选参数」在命令行里天然有歧义（下一个词
        // 也可能是分子文件路径）。所以只在它确实能解析成形状名时才吃掉它。
        if (arg == "--dump-orbital")
        {
            dumpOrbital    = true;
            dumpOrbitalAll = true;

            if (i + 1 < argc)
            {
                orbital::ShapeKind k = orbital::ShapeKind::S;
                if (orbital::parseShapeKind(argv[i + 1], &k))
                {
                    dumpOrbitalKind = k;
                    dumpOrbitalAll  = false;
                    ++i;
                }
            }
            continue;
        }

        // 第 20 天：--dump-cloud [n + 形状]
        // 和 --dump-orbital 一样，「可选参数」只在它确实能解析成轨道时才吃掉，
        // 否则下一个词可能是分子文件路径。
        if (arg == "--dump-cloud")
        {
            dumpCloud = true;

            if (i + 1 < argc)
            {
                int                nn = 1;
                orbital::ShapeKind k  = orbital::ShapeKind::S;
                if (orbital::parseOrbitalSpec(argv[i + 1], &nn, &k))
                {
                    dumpCloudN    = nn;
                    dumpCloudKind = k;
                    dumpCloudSpec = true;
                    ++i;
                }
                else
                {
                    // 以数字开头就说明用户想写轨道（2d、7s、0s…），只是组合不存在。
                    // 这种要报错，不能默默按「没给参数」处理——那样会打一张
                    // 摘要表，用户还以为 2d 被接受了。
                    const std::string next = argv[i + 1];
                    if (!next.empty() && std::isdigit(static_cast<unsigned char>(next[0])))
                        dumpCloudBad = next;
                }
            }
            continue;
        }

        userPath = arg;
    }
    // ---------- 元素数据库 ----------
    // 放在 GLFW 初始化之前：--dump 是纯数据层验证，不需要窗口，
    // 数据和渲染彻底解耦，headless（无显卡/CI）环境也能跑。
    std::string dbError;
    const bool dbOk = loadElementDatabase(&dbError);
    if (!dbOk)
    {
        std::cerr << "[错误] 元素数据库加载失败: " << dbError << "\n";
        std::cerr << "       分子查看器继续运行，但 Atomotheca 面板不可用。\n";
    }
    else
    {
        std::cout << "元素数据库: " << g_elements.size() << " 个元素";
        // 顺带打印氧的排布，一眼确认数据层是通的
        if (const orbital::Element* oxygen = g_elements.find("O"))
            std::cout << "  |  示例 O: " << oxygen->configString();
        std::cout << std::endl;
    }

    // ---------- --dump ----------
    if (!dumpSymbol.empty())
    {
        if (!dbOk)
            return -1;

        const orbital::Element* element = findElementLoose(dumpSymbol);

        if (element == nullptr)
        {
            std::cerr << "[错误] 数据库里没有元素: " << dumpSymbol << "\n";
            std::cerr << "       当前可用: ";
            for (const orbital::Element& e : g_elements.elements())
                std::cerr << e.symbol << ' ';
            std::cerr << "\n";
            return -1;
        }

        std::cout << orbital::describe(*element);
        return 0;
    }

    // ---------- --dump-table ----------
    // 窗口在这一层验证不了（开发环境没有 GL 上下文），
    // 但排布逻辑是纯计算，用终端输出就能核对 18 族的定位对不对。
    if (dumpTable)
    {
        if (!dbOk)
            return -1;
        ui::printPeriodicTableAscii(g_elements);
        return 0;
    }

    // ---------- --dump-levels ----------
    // 和 --dump-table 同一个思路：窗口在这一层验证不了，但「谁在谁上方」是纯数据。
    // 一次把中性原子和所有常见离子都打出来，顺便核一遍 makeIon() 的推算结果。
    if (!dumpLevelsSymbol.empty())
    {
        if (!dbOk)
            return -1;

        const orbital::Element* element = findElementLoose(dumpLevelsSymbol);
        if (element == nullptr)
        {
            std::cerr << "[错误] 数据库里没有元素: " << dumpLevelsSymbol << "\n";
            std::cerr << "       当前可用: ";
            for (const orbital::Element& e : g_elements.elements())
                std::cerr << e.symbol << ' ';
            std::cerr << "\n";
            return -1;
        }

        ui::printEnergyDiagramAscii(g_elements, element->atomicNumber, 0);
        for (int q : element->commonIonCharges)
            ui::printEnergyDiagramAscii(g_elements, element->atomicNumber, q);
        return 0;
    }

    // ---------- --dump-orbital ----------
    // 几何是纯数学，不需要 GL 上下文，也不需要元素数据库：
    // 所以这一条放在数据库加载之后、GLFW 之前，无显卡环境同样能跑。
    if (dumpOrbital)
    {
        if (dumpOrbitalAll)
        {
            for (int k = 0; k < orbital::SHAPE_COUNT; ++k)
            {
                std::cout << orbital::describeShape(static_cast<orbital::ShapeKind>(k));
                std::cout << "\n";
            }
        }
        else
        {
            std::cout << orbital::describeShape(dumpOrbitalKind);
        }
        return 0;
    }

    // ---------- --dump-cloud ----------
    // 采样也是纯数学：不开窗口、不需要元素数据库，无显卡环境照样能跑。
    if (dumpCloud)
    {
        const int kDumpPoints = 20000;   // 统计量在 2 万点下已经够用，全打一遍只要几十毫秒

        if (!dumpCloudBad.empty())
        {
            std::cerr << "[错误] 不存在这个轨道: " << dumpCloudBad << "\n";
            std::cerr << "       写法是「主量子数 + 形状」，例如 3dz2 / 4p_x / 1s。\n";
            std::cerr << "       n 只能取 1..6，并且 l <= n-1（所以 1p、2d 都不存在）；\n"
                      << "       角向部分目前只做到 s / p / d，f 及更高还没实现。\n";
            return -1;
        }

        if (dumpCloudSpec)
        {
            std::cout << orbital::describeCloud(dumpCloudN, dumpCloudKind, kDumpPoints);
        }
        else
        {
            // 默认列表特意把 1s / 2s / 3s 排在一起：三者的角向部分完全一样，
            // 只有径向不同。Day 19 的 |Y| 形状画出来是同一颗球，这里却能看出
            // 「一个球套一个球」——这就是今天补上 R_nl 的意义。
            struct Entry
            {
                int                n;
                orbital::ShapeKind kind;
            };
            static const Entry kDefaults[] = {
                { 1, orbital::ShapeKind::S   },
                { 2, orbital::ShapeKind::S   },
                { 3, orbital::ShapeKind::S   },
                { 2, orbital::ShapeKind::Pz  },
                { 3, orbital::ShapeKind::Pz  },
                { 3, orbital::ShapeKind::Dz2 },
                { 3, orbital::ShapeKind::Dxy },
                { 4, orbital::ShapeKind::S   },
            };

            std::cout << "electron cloud sampling check   (20000 points each, seed = 1)\n";
            std::cout << "  <r> / <r2> : sample vs closed form      cos2 : sample vs sphere integral\n";
            std::cout << "  r98 : radius holding 98% (a0)           nodes : measured / n-l-1\n\n";

            for (const Entry& e : kDefaults)
                std::cout << orbital::cloudSummaryLine(e.n, e.kind, kDumpPoints) << "\n";

            std::cout << "\nuse --dump-cloud 3dz2 for one orbital in full (stats + ASCII slice)\n";
        }
        return 0;
    }
#else
    // Web：资源已打包进虚拟文件系统，路径固定
    std::string dbError;
    const bool dbOk = loadElementDatabase(&dbError);
    if (!dbOk)
        std::cerr << "[错误] 元素数据库加载失败: " << dbError << std::endl;
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
    // 先挂错误回调：glfwInit() / glfwCreateWindow() 失败时只返回 false，
    // 不告诉你原因。挂上之后 GLFW 会把具体错误打到 stderr，
    // 例如 "No supported platform was found"（GLFW 没编进任何窗口后端）。
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit())
    {
        std::cerr << "[错误] GLFW 初始化失败！" << std::endl;
        std::cerr << "       常见原因：" << std::endl;
        std::cerr << "       1. 没有图形界面（SSH / 纯 TTY），DISPLAY 未设置" << std::endl;
        std::cerr << "       2. GLFW 编译时没启用 X11 / Wayland 后端（见上方 GLFW 错误码）" << std::endl;
        std::cerr << "       3. 缺少 libGL / 显卡驱动" << std::endl;
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
        std::cerr << "       GLFW 初始化成功，但拿不到 OpenGL 3.3 Core 上下文。" << std::endl;
        std::cerr << "       如果上面出现 'GLX: Failed to create context'，说明是驱动/GLX 的问题：" << std::endl;
        std::cerr << "         - 虚拟机、远程桌面、无 GPU 的环境常常提供不了 3.3 Core" << std::endl;
        std::cerr << "         - 用 glxinfo -B 自查：Renderer 是 llvmpipe 或直接报错，就是软件渲染/没装驱动" << std::endl;
        std::cerr << "         - NVIDIA 显卡需要装闭源驱动，nouveau 通常到不了 3.3 Core" << std::endl;
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

    // 第 20 天：Core Profile 里 gl_PointSize 默认**不生效** ——
    // 不开这个开关，所有点都会是最小的 1 像素，点云看起来像一层噪点。
    // 它不是渲染循环里的逐帧状态，只在启动时开一次。
    //
    // Web（GLES3）不需要也不该调：那边 gl_PointSize 一直有效，
    // 而且 ES 的头文件里不一定有这个常量。
#ifndef __EMSCRIPTEN__
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif

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
    setupOrbitalMeshes();     // 第 18 天：s / p 轨道形状（4 个形状各一份）

    // ---------- 着色器 ----------
    std::string svPath = pathutil::resolveResource("src/shaders/sphere.vert");
    std::string sfPath = pathutil::resolveResource("src/shaders/sphere.frag");
    std::string cvPath = pathutil::resolveResource("src/shaders/cylinder.vert");
    std::string cfPath = pathutil::resolveResource("src/shaders/cylinder.frag");
    std::string ovPath = pathutil::resolveResource("src/shaders/orbital.vert");
    std::string ofPath = pathutil::resolveResource("src/shaders/orbital.frag");
    std::string clvPath = pathutil::resolveResource("src/shaders/cloud.vert");
    std::string clfPath = pathutil::resolveResource("src/shaders/cloud.frag");

    if (svPath.empty() || sfPath.empty() || cvPath.empty() || cfPath.empty() ||
        ovPath.empty() || ofPath.empty() || clvPath.empty() || clfPath.empty())
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
    g_app.orbitalShader  = new Shader(ovPath.c_str(), ofPath.c_str());
    g_app.cloudShader    = new Shader(clvPath.c_str(), clfPath.c_str());

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

        GLuint oid = g_app.orbitalShader->ID;
        g_app.o_model     = glGetUniformLocation(oid, "model");
        g_app.o_view      = glGetUniformLocation(oid, "view");
        g_app.o_proj      = glGetUniformLocation(oid, "projection");
        g_app.o_normalMat = glGetUniformLocation(oid, "normalMatrix");
        g_app.o_color     = glGetUniformLocation(oid, "shapeColor");
        g_app.o_alpha     = glGetUniformLocation(oid, "baseAlpha");
        g_app.o_viewPos   = glGetUniformLocation(oid, "viewPos");
        g_app.o_lightPos  = glGetUniformLocation(oid, "lightPos");

        GLuint clid = g_app.cloudShader->ID;
        g_app.cl_model     = glGetUniformLocation(clid, "model");
        g_app.cl_view      = glGetUniformLocation(clid, "view");
        g_app.cl_proj      = glGetUniformLocation(clid, "projection");
        g_app.cl_viewPos   = glGetUniformLocation(clid, "viewPos");
        g_app.cl_sizeWorld = glGetUniformLocation(clid, "pointSizeWorld");
        g_app.cl_sizeScale = glGetUniformLocation(clid, "sizeScale");
        g_app.cl_colorPos  = glGetUniformLocation(clid, "colorPositive");
        g_app.cl_colorNeg  = glGetUniformLocation(clid, "colorNegative");
        g_app.cl_opacity   = glGetUniformLocation(clid, "opacity");
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
    delete g_app.orbitalShader;
    delete g_app.cloudShader;
    g_app.sphereShader = nullptr;
    g_app.cylinderShader = nullptr;
    g_app.orbitalShader = nullptr;
    g_app.cloudShader = nullptr;

    glDeleteVertexArrays(1, &g_app.sphereVAO);
    glDeleteBuffers(1, &g_app.sphereVBO);
    glDeleteBuffers(1, &g_app.sphereEBO);
    glDeleteVertexArrays(1, &g_app.cylVAO);
    glDeleteBuffers(1, &g_app.cylVBO);
    glDeleteBuffers(1, &g_app.cylEBO);

    for (int i = 0; i < orbital::SHAPE_COUNT; ++i)
    {
        glDeleteVertexArrays(1, &g_app.orbVAO[i]);
        glDeleteBuffers(1, &g_app.orbVBO[i]);
        glDeleteBuffers(1, &g_app.orbEBO[i]);
    }

    // 第 20 天：点云。VAO 为 0 表示从没显示过（没建过缓冲），跳过。
    if (g_app.cloudVAO != 0)
    {
        glDeleteVertexArrays(1, &g_app.cloudVAO);
        glDeleteBuffers(1, &g_app.cloudVBO);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
