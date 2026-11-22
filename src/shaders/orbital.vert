#version 330 core

// ============================================================
// 第 18 天：轨道形状的顶点着色器
//   和 sphere.vert 几乎一样，去掉分子球专用的 normalOffset
//   （那是给空间填充模型压 Z-fighting 用的，轨道不需要）
// ============================================================

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;

out vec3 FragPos;
out vec3 Normal;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // 世界空间位置：光照与 Fresnel 都要在这里算
    FragPos = vec3(model * vec4(aPos, 1.0));

    // 法线不能直接乘 model（非均匀缩放会把法线拉歪），用法线矩阵
    Normal = normalize(normalMatrix * aNormal);
}
