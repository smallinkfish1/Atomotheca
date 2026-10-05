// src/main.cpp
// 第 4 天：GLM + 模型/观察/投影矩阵，画一个旋转的线框立方体。

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "shader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
                                          "Molecule Viewer - Day 4", nullptr, nullptr);
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

    // 打开深度测试：让前后遮挡正确
    glEnable(GL_DEPTH_TEST);

    // 打开线框模式：把三角形只画轮廓
    // 想切回实心，改成 glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // ==================== 立方体顶点数据 ====================
    // 立方体 8 个顶点，范围 -0.5 ~ 0.5，中心在原点
    float vertices[] = {
        -0.5f, -0.5f, -0.5f, // 0
         0.5f, -0.5f, -0.5f, // 1
         0.5f,  0.5f, -0.5f, // 2
        -0.5f,  0.5f, -0.5f, // 3
        -0.5f, -0.5f,  0.5f, // 4
         0.5f, -0.5f,  0.5f, // 5
         0.5f,  0.5f,  0.5f, // 6
        -0.5f,  0.5f,  0.5f  // 7
    };

    // 12 个三角形 = 6 个面，每个面 2 个三角形
    unsigned int indices[] = {
        // 后面 (z = -0.5)
        0, 1, 2,  2, 3, 0,
        // 前面 (z = +0.5)
        4, 5, 6,  6, 7, 4,
        // 左面 (x = -0.5)
        0, 3, 7,  7, 4, 0,
        // 右面 (x = +0.5)
        1, 5, 6,  6, 2, 1,
        // 下面 (y = -0.5)
        0, 4, 5,  5, 1, 0,
        // 上面 (y = +0.5)
        3, 2, 6,  6, 7, 3
    };

    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // 只有位置属性，3 个 float，步长 3*sizeof(float)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    Shader cubeShader("src/shaders/cube.vert", "src/shaders/cube.frag");

    // ==================== 渲染循环 ====================
    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        // 清颜色 + 清深度（深度别忘！）
        glClearColor(0.12f, 0.12f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        cubeShader.use();

        // ---------- 1. 模型矩阵：绕 y 轴旋转 ----------
        glm::mat4 model = glm::mat4(1.0f);              // 单位矩阵（什么都不做）
        float angle = (float)glfwGetTime();             // 随时间递增的角度（弧度）
        model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));

        // ---------- 2. 观察矩阵：相机后退 ----------
        glm::mat4 view = glm::mat4(1.0f);
        view = glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));

        // ---------- 3. 投影矩阵：透视投影 ----------
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT,
            0.1f,
            100.0f);

        // 把三个矩阵传给着色器
        glUniformMatrix4fv(glGetUniformLocation(cubeShader.ID, "model"),
                           1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(cubeShader.ID, "view"),
                           1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(cubeShader.ID, "projection"),
                           1, GL_FALSE, glm::value_ptr(projection));

        // 设置物体颜色（青色）
        glUniform3f(glGetUniformLocation(cubeShader.ID, "objectColor"),
                    0.3f, 0.9f, 1.0f);

        // 画立方体：36 个索引 = 12 个三角形
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ==================== 清理 ====================
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "程序正常退出。" << std::endl;
    return 0;
}
