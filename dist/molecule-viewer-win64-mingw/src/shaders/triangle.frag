#version 330 core
// 片段着色器：对每个像素（片段）执行一次

in vec3 vertexColor;   // 接收顶点着色器传来的颜色
out vec4 FragColor;    // 输出这个像素的最终颜色 RGBA

void main()
{
    FragColor = vec4(vertexColor, 1.0);
}
