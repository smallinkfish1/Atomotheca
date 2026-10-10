#version 330 core

in vec3 FragPos;    // 世界空间位置（来自顶点着色器）
in vec3 Normal;     // 世界空间法线（来自顶点着色器）

out vec4 FragColor;

// 材质与光照参数
uniform vec3 objectColor;    // 物体本身的颜色
uniform vec3 lightColor;     // 光的颜色
uniform vec3 lightPos;       // 光源位置（世界空间）
uniform vec3 viewPos;        // 相机位置（世界空间）

void main()
{
    // ---------- 1. 环境光：给个基础亮度，避免背光面全黑 ----------
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * lightColor;

    // ---------- 2. 漫反射：立体感的主要来源 ----------
    // 光线方向 = 从物体表面指向光源（并归一化）
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    // dot 衡量法线和光线的"同向程度"
    // max(..., 0.0) 把背光面压成 0
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // ---------- 3. 合并 ----------
    vec3 result = (ambient + diffuse) * objectColor;
    FragColor = vec4(result, 1.0);
}
