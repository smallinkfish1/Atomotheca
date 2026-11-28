#version 330 core

// ============================================================
// 第 20 天：电子云点云的顶点着色器
//
// 和其他着色器最大的区别：顶点不是三角形，而是一个像素点。
// 所以这里唯一的新东西是 gl_PointSize —— 点要有透视缩小，
// 否则相机拉近时整朵云会保持同样粗细，看起来像贴纸。
// （Core Profile 下 gl_PointSize 必须开 GL_PROGRAM_POINT_SIZE 才生效）
// ============================================================

layout (location = 0) in vec3  aPos;     // 位置，单位 a0
layout (location = 1) in float aPhase;   // psi 的符号，+1 / -1

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 viewPos;

uniform float pointSizeWorld;   // 一个点的世界直径（CPU 按云半径的比例算好）
uniform float sizeScale;        // (视口高/2) / tan(fov/2)，把世界尺寸换成像素

out float vPhase;

void main()
{
    vec4 world = model * vec4(aPos, 1.0);
    gl_Position = projection * view * world;

    // 透视缩放：距离 d 处，1 个世界单位对应 sizeScale/d 个像素
    float dist = max(length(vec3(world) - viewPos), 0.001);
    gl_PointSize = clamp(pointSizeWorld * sizeScale / dist, 1.0, 48.0);

    vPhase = aPhase;
}
