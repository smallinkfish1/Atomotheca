#version 330 core

// 顶点属性：位置(location 0) + 法线(location 1)
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

// 三大矩阵
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// 法线矩阵：正确的法线变换
uniform mat3 normalMatrix;

// 传给片段着色器的数据
out vec3 FragPos;      // 世界空间中的位置
out vec3 Normal;       // 世界空间中的法线

void main()
{
    // 把顶点变换到裁剪空间
    gl_Position = projection * view * model * vec4(aPos, 1.0);

    // 世界空间位置：直接用 model 变换
    FragPos = vec3(model * vec4(aPos, 1.0));

    // 世界空间法线：用法线矩阵变换，再归一化
    Normal = normalize(normalMatrix * aNormal);
}
