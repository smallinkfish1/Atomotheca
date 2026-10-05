#version 330 core
// 矩形用：顶点带位置（location 0）和颜色（location 1）

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 vertexColor;

void main()
{
    gl_Position = vec4(aPos, 1.0);
    vertexColor = aColor;   // 直接把顶点自带的颜色传下去
}
