// src/shader.h
// 着色器封装：读文件 + 编译 + 链接。
//
// 第 14 天：增加 Web（WebGL2 / GLSL ES 3.00）适配。
//   - 桌面：GLAD + #version 330 core
//   - Web ：Emscripten 自带 GLES3，着色器需改为 #version 300 es，
//           且片段着色器必须声明浮点精度

#ifndef SHADER_H
#define SHADER_H

// ---------- 平台相关的 OpenGL 头 ----------
#ifdef __EMSCRIPTEN__
#  include <GLES3/gl3.h>          // Emscripten 提供的 GLES3（映射到 WebGL2）
#else
#  include <glad/glad.h>          // 桌面：用 GLAD 加载函数指针
#endif

#include <GLFW/glfw3.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

class Shader
{
public:
    unsigned int ID = 0;

    Shader(const char* vertexPath, const char* fragmentPath);
    void use() const;
    ~Shader();

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

private:
    static unsigned int compileShader(unsigned int type, const std::string& source);

    // 把桌面版 GLSL 转换成 WebGL2 能用的 GLSL ES 3.00
    static std::string adaptGLSL(const std::string& source, bool isFragment);
};

// ============================================================
// GLSL 版本适配
//
// 桌面：  #version 330 core
// WebGL2：#version 300 es  + 片段着色器需要 "precision highp float;"
//
// 我们的着色器代码本身是兼容的（都用 in/out、没有用废弃语法），
// 所以只需要改版本行、补精度声明。
// ============================================================
inline std::string Shader::adaptGLSL(const std::string& source, bool isFragment)
{
#ifdef __EMSCRIPTEN__
    std::string s = source;

    // ---- 1. 替换版本行 ----
    // "#version 330 core" 共 17 个字符
    const std::string oldVer = "#version 330 core";
    const std::string newVer = "#version 300 es";

    size_t pos = s.find(oldVer);
    if (pos != std::string::npos)
        s.replace(pos, oldVer.size(), newVer);

    // ---- 2. 片段着色器补精度声明 ----
    // GLSL ES 规定：片段着色器必须显式声明 float 精度，否则编译报错
    if (isFragment)
    {
        size_t vpos = s.find("#version 300 es");
        if (vpos != std::string::npos)
        {
            size_t lineEnd = s.find('\n', vpos);
            if (lineEnd != std::string::npos)
                s.insert(lineEnd + 1, "\nprecision highp float;\n");
        }
        else
        {
            // 极端情况：文件里没有版本行，直接插在开头
            s = "precision highp float;\n" + s;
        }
    }

    return s;
#else
    (void)isFragment;
    return source;
#endif
}

inline unsigned int Shader::compileShader(unsigned int type, const std::string& source)
{
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

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
    std::string vertexCode, fragmentCode;
    std::ifstream vFile, fFile;

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

    // ★ 第 14 天：按平台适配 GLSL 版本
    vertexCode   = adaptGLSL(vertexCode,   false);
    fragmentCode = adaptGLSL(fragmentCode, true);

    unsigned int vertex   = compileShader(GL_VERTEX_SHADER,   vertexCode);
    unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentCode);

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);

    int success = 0;
    char infoLog[1024];
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(ID, 1024, nullptr, infoLog);
        std::cerr << "[着色器链接失败]\n" << infoLog << std::endl;
    }

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
