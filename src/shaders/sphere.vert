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

// 新增（第 8 天）：沿法线的微偏移量，用于缓解相交曲面的 Z-fighting
uniform float normalOffset;

// 传给片段着色器的数据
out vec3 FragPos;      // 世界空间中的位置
out vec3 Normal;       // 世界空间中的法线

void main()
{
    // 把顶点沿法线方向轻微外推（normalOffset > 0）或内缩（< 0）
    // 默认传 0.0，不影响原有行为
    vec3 offsetPos = aPos + aNormal * normalOffset;

    // 把顶点变换到裁剪空间
    gl_Position = projection * view * model * vec4(offsetPos, 1.0);

    // 世界空间位置
    FragPos = vec3(model * vec4(offsetPos, 1.0));

    // 世界空间法线：用法线矩阵变换，再归一化
    Normal = normalize(normalMatrix * aNormal);
}
