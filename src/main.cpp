#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <cmath>         // 用到 std::sin

const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

static void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height)
{
    glViewport(0, 0, width, height);
}

static void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

int main()
{
    // ---------- 1. 初始化 GLFW ----------
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

    // ---------- 2. 创建窗口 + 上下文 ----------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                                          "Molecule Viewer - Day 2", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建 GLFW 窗口失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);                 // 必须先有上下文
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // ---------- 3. 初始化 GLAD ----------
    // 必须在 glfwMakeContextCurrent 之后！
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "[错误] GLAD 初始化失败！" << std::endl;
        glfwTerminate();
        return -1;
    }

    // 打印一下显卡信息，确认 GLAD 真的接上了（第 1 天 glxinfo 的代码版）
    std::cout << "OpenGL 版本: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "显卡厂商:   " << glGetString(GL_RENDERER) << std::endl;

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    // ---------- 4. 渲染循环 ----------
    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        // 用时间算一个 0.0~1.0 的振荡值
        float t = (float)glfwGetTime();
        float r = 0.5f * (float)std::sin(t)        + 0.5f;  // 红
        float g = 0.5f * (float)std::sin(t + 2.0f) + 0.5f;  // 绿（相位偏移 2.0）
        float b = 0.5f * (float)std::sin(t + 4.0f) + 0.5f;  // 蓝（相位偏移 4.0）

        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
