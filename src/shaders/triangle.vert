#version 330 core
// 顶点着色器：对每个顶点执行一次

// 输入：从 VBO 传来的顶点位置（location = 0 是约定的位置编号）
layout (location = 0) in vec3 aPos;

// 输出：传给片段着色器的颜色（这里随便给个白色，用于简单验证）
out vec3 vertexColor;

void main()
{
    // gl_Position 是内置变量，必须赋值，表示这个顶点最终在 NDC 里的位置
    // vec4 的第四个分量 w 固定填 1.0（齐次坐标，第 4 天详细讲）
    gl_Position = vec4(aPos, 1.0);

    // 把颜色往下传
    vertexColor = vec3(1.0, 1.0, 1.0);
}
