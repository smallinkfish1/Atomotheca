#version 330 core

// ============================================================
// 第 9 天：完整的 Blinn-Phong 光照
//   环境光 Ambient + 漫反射 Diffuse + 镜面高光 Specular
//   并引入"补光"（Fill Light）修复暗部死黑的问题
// ============================================================

in vec3 FragPos;    // 世界空间位置
in vec3 Normal;     // 世界空间法线

out vec4 FragColor;

// ---------- 材质 ----------
uniform vec3 objectColor;        // 物体本身的颜色

// ---------- 主光（Key Light）----------
uniform vec3 lightPos;           // 主光位置
uniform vec3 lightColor;         // 主光颜色

// ---------- 补光（Fill Light）----------
uniform vec3 fillLightPos;       // 补光位置
uniform vec3 fillLightColor;     // 补光颜色

// ---------- 相机 ----------
uniform vec3 viewPos;            // 相机位置（算镜面高光要用）

void main()
{
    vec3 norm = normalize(Normal);

    // ==================== 1. 环境光 ====================
    // 模拟环境中的多次反射光，避免背光面变成纯黑
    // 0.35 是"够亮但不失真"的经验值
    float ambientStrength = 0.35;
    vec3 ambient = ambientStrength * lightColor;

    // ==================== 2. 主光漫反射 ====================
    // 立体感的主要来源
    vec3  lightDir = normalize(lightPos - FragPos);
    float diff     = max(dot(norm, lightDir), 0.0);
    vec3  diffuse  = diff * lightColor;

    // ==================== 3. 补光漫反射 ====================
    // 从主光的"对侧"再打一盏弱光
    // 作用：填补主光照不到的死角，让暗部有层次，而不是一片死黑
    // 这是修复"黑斑 bug"的关键
    vec3  fillDir     = normalize(fillLightPos - FragPos);
    float fillDiff    = max(dot(norm, fillDir), 0.0);
    vec3  fillDiffuse = fillDiff * fillLightColor;

    // ==================== 4. 镜面高光（Blinn-Phong）====================
    // 模拟光滑表面的反光点，让球体有"抛光"的质感
    float specularStrength = 0.55f;   // 高光强度
    float shininess        = 64.0f;   // 光泽度：越大高光越锐利、越像光滑表面

    vec3 viewDir    = normalize(viewPos - FragPos);    // 从表面指向相机
    vec3 halfwayDir = normalize(lightDir + viewDir);   // 半程向量（Blinn-Phong 的核心）
    float spec      = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    vec3 specular   = specularStrength * spec * lightColor;

    // ==================== 5. 合并 ====================
    // 漫反射部分乘以物体颜色（物体颜色决定"看起来是什么颜色"）
    // 高光部分不乘物体颜色（高光反映的是光源的颜色，像塑料的反光）
    vec3 result = (ambient + diffuse + fillDiffuse) * objectColor + specular;

    FragColor = vec4(result, 1.0);
}
