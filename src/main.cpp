// src/main.cpp
// 第 3 天：VBO / VAO / EBO + 着色器，画出三角形和矩形。
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "shader.h"

#include <iostream>

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
                                          "Molecule Viewer - Day 3", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[错误] 创建窗口失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "[错误] GLAD 初始化失败！" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "OpenGL: " << glGetString(GL_VERSION)
    << " | " << glGetString(GL_RENDERER) << std::endl;

    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    // ==================== 第一部分：三角形 ====================
    // 3 个顶点，每个顶点 3 个分量 (x, y, z)
    float triangleVertices[] = {
        // 位置              // 颜色（用不到但先留着方便对照）
        -0.9f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
        0.9f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
        0.0f,  0.7f, 0.0f,  0.0f, 0.0f, 1.0f,
    };

    unsigned int triVAO, triVBO;
    glGenVertexArrays(1, &triVAO);   // 生成 1 个 VAO
    glGenBuffers(1, &triVBO);        // 生成 1 个 VBO

    glBindVertexArray(triVAO);       // 绑定 VAO（之后的配置记在它身上）

    glBindBuffer(GL_ARRAY_BUFFER, triVBO);   // 绑定 VBO 到"数组缓冲"目标
    // 把顶点数据上传到 GPU：GL_STATIC_DRAW 表示数据几乎不变（性能提示）
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangleVertices),
                 triangleVertices, GL_STATIC_DRAW);

    // 告诉 OpenGL 如何解读 VBO 里的数据
    // 参数：属性编号, 分量个数, 类型, 是否归一化, 步长(字节), 起始偏移
    // 步长 = 6 个 float（位置3 + 颜色3），所以是 6 * sizeof(float)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);   // 启用 0 号属性

    // 我们用不到颜色属性也可以不启用；这里为了让数据格式一致，启用 1 号
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 解绑（好习惯：避免之后误改）
    glBindVertexArray(0);

    Shader triShader("src/shaders/triangle.vert", "src/shaders/triangle.frag");

    // ==================== 第二部分：矩形（带 EBO） ====================
    // 4 个顶点，每个 6 个 float（位置 + 颜色）
    float rectVertices[] = {
        // 位置              // 颜色
        0.5f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // 右上 索引0
        0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // 右下 索引1
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  // 左下 索引2
        -0.5f,  0.5f, 0.0f,  1.0f, 1.0f, 0.0f,  // 左上 索引3
    };
    // 用 6 个索引画出 2 个三角形
    unsigned int rectIndices[] = {
        0, 1, 2,   // 第一个三角形
        0, 2, 3    // 第二个三角形
    };

    unsigned int rectVAO, rectVBO, rectEBO;
    glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glGenBuffers(1, &rectEBO);

    glBindVertexArray(rectVAO);

    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectVertices), rectVertices, GL_STATIC_DRAW);

    // 注意：EBO 的绑定会被 VAO 记住，所以必须在 VAO 激活时绑定
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rectEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(rectIndices), rectIndices, GL_STATIC_DRAW);

    // 位置属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 颜色属性
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    Shader rectShader("src/shaders/rect.vert", "src/shaders/rect.frag");

    // ==================== 渲染循环 ====================
    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        glClearColor(0.12f, 0.12f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 画三角形：用 glDrawArrays（按顺序取顶点）
        triShader.use();
        glBindVertexArray(triVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);   // 从第 0 个顶点开始，共 3 个

        // 画矩形：用 glDrawElements（按 EBO 里的索引取顶点）
        rectShader.use();
        glBindVertexArray(rectVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);  // 6 个索引

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ==================== 清理 ====================
    glDeleteVertexArrays(1, &triVAO);
    glDeleteBuffers(1, &triVBO);
    glDeleteVertexArrays(1, &rectVAO);
    glDeleteBuffers(1, &rectVBO);
    glDeleteBuffers(1, &rectEBO);

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
