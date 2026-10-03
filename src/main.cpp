// #include <glad/glad.h>   // GLAD：负责加载 OpenGL 函数（第 2 天用，今天先占位说明）
#include <GLFW/glfw3.h>  // GLFW：负责窗口和输入
#include <iostream>
// 窗口大小（像素）
const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// 这个函数会在"窗口大小改变"时被 GLFW 自动调用
static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // 告诉 OpenGL：视口应该铺满新的窗口大小
    // 前两个参数是左下角坐标（0,0），后两个是宽高
    glViewport(0, 0, width, height);
}

// 处理用户输入（这里只处理"按 ESC 关闭窗口"）
static void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    {
        // 设置窗口应该关闭的标志，下一轮循环时会退出
        glfwSetWindowShouldClose(window, true);
    }
}

int main()
{
    // ---------- 1. 初始化 GLFW ----------
    if (!glfwInit())
    {
        std::cerr << "[错误] GLFW 初始化失败！" << std::endl;
        return -1;
    }

    // 告诉 GLFW：我们要用 OpenGL 3.3 的 Core Profile（核心模式）
    // 核心模式 = 现代 OpenGL，没有那些古老的、废弃的函数，正是我们要学的
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    #ifdef __APPLE__
    // macOS 要求必须显式声明"向前兼容"，否则用不了 3.3 核心模式
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // ---------- 2. 创建窗口 ----------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                                          "Molecule Viewer - Day 1", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建 GLFW 窗口失败！" << std::endl;
        glfwTerminate(); // 收尾，释放 GLFW 占用的资源
        return -1;
    }

    // 把这个窗口的 OpenGL 上下文设为"当前主上下文"
    // 之后所有 OpenGL 命令都会作用在这个窗口上
    glfwMakeContextCurrent(window);

    // 注册回调函数：窗口大小变化时自动调用上面写的函数
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // ---------- 3. 加载 OpenGL 函数（第 2 天会用到 GLAD） ----------
    // 今天只是空窗口，不调用任何 OpenGL 绘图函数，
    // 所以这里暂时不需要 GLAD。第 2 天会正式启用。

    // 告诉 OpenGL 初始视口大小（此时窗口刚创建，设置一次）
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    // ---------- 4. 渲染循环（Render Loop）----------
    // 只要窗口不该关闭，就一直循环
    while (!glfwWindowShouldClose(window))
    {
        // (1) 处理输入
        processInput(window);

        // (2) 清屏：用深灰蓝色填充整块画布
        //     glClearColor 设置"清屏用什么颜色"，范围 0.0~1.0（红,绿,蓝,透明度）
        glClearColor(0.12f, 0.12f, 0.18f, 1.0f);
        //     glClear 真正执行清屏
        glClear(GL_COLOR_BUFFER_BIT);

        // (3) 交换前后缓冲，把画好的内容显示到屏幕
        glfwSwapBuffers(window);

        // (4) 处理所有待处理的系统事件（键盘、鼠标、窗口事件）
        glfwPollEvents();
    }

    // ---------- 5. 收尾 ----------
    glfwDestroyWindow(window); // 销毁窗口
    glfwTerminate();           // 终止 GLFW

    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
