#version 330 core

layout (location = 0) in vec3 aPos;

// uniform = 从 CPU 传进来的、对所有顶点都相同的变量
// 三个矩阵：模型、观察、投影
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // 注意乘法顺序：projection * view * model * 顶点
    // 从右往左作用：先摆到世界位置，再看相机视角，最后投影
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
