// src/shader.h
// 一个小小的着色器封装：读文件 + 编译 + 链接，帮你省掉重复代码。
// 刻意写得很简单，方便你读懂每一行。
#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader
{
public:
    // 着色器程序的 OpenGL ID
    unsigned int ID = 0;

    // 用两个文件路径构造并编译链接
    Shader(const char* vertexPath, const char* fragmentPath);

    // 激活这个着色器程序（画东西之前调用）
    void use() const;

    // 析构时自动删除 GPU 上的程序对象，避免泄漏
    ~Shader();

    // 禁止拷贝，避免同一个程序被删两次
    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

private:
    // 编译单个着色器并返回其 ID
    static unsigned int compileShader(unsigned int type, const std::string& source);
};

// ---------- 以下是实现（故意放在头文件里，方便你一处查看） ----------

inline unsigned int Shader::compileShader(unsigned int type, const std::string& source)
{
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    // 检查编译是否成功，失败时打印日志（新手排错最重要的信息）
    int success = 0;
    char infoLog[1024];
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(id, 1024, nullptr, infoLog);
        std::cerr << "[着色器编译失败] "
        << (type == GL_VERTEX_SHADER ? "顶点着色器" : "片段着色器")
        << "\n" << infoLog << std::endl;
    }
    return id;
}

inline Shader::Shader(const char* vertexPath, const char* fragmentPath)
{
    // 1. 从文件读取源码
    std::string vertexCode, fragmentCode;
    std::ifstream vFile, fFile;

    // 打开失败时抛异常，避免静默失败（新手最怕"没报错但不对"）
    vFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try
    {
        vFile.open(vertexPath);
        fFile.open(fragmentPath);

        std::stringstream vStream, fStream;
        vStream << vFile.rdbuf();
        fStream << fFile.rdbuf();

        vFile.close();
        fFile.close();

        vertexCode   = vStream.str();
        fragmentCode = fStream.str();
    }
    catch (std::ifstream::failure& e)
    {
        std::cerr << "[错误] 着色器文件读取失败: " << e.what()
        << "\n  vertexPath   = " << vertexPath
        << "\n  fragmentPath = " << fragmentPath << std::endl;
    }

    // 2. 编译
    unsigned int vertex   = compileShader(GL_VERTEX_SHADER,   vertexCode);
    unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentCode);

    // 3. 链接成一个程序
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);

    // 检查链接
    int success = 0;
    char infoLog[1024];
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(ID, 1024, nullptr, infoLog);
        std::cerr << "[着色器链接失败]\n" << infoLog << std::endl;
    }

    // 4. 链接完就可以删掉单独的着色器对象了（程序已经持有它们）
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

inline void Shader::use() const
{
    glUseProgram(ID);
}

inline Shader::~Shader()
{
    if (ID != 0)
        glDeleteProgram(ID);
}

#endif // SHADER_H
