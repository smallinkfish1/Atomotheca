#version 330 core

// ============================================================
// 第 11 天：光照参数改为 uniform，可由 ImGui 面板实时调节
//   环境光 Ambient + 主光漫反射 + 补光漫反射 + 镜面高光 Specular
// ============================================================

in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

// ---------- 材质 ----------
uniform vec3 objectColor;

// ---------- 主光（Key Light）----------
uniform vec3 lightPos;
uniform vec3 lightColor;

// ---------- 补光（Fill Light）----------
uniform vec3 fillLightPos;
uniform vec3 fillLightColor;

// ---------- 相机 ----------
uniform vec3 viewPos;

// ---------- 光照参数（今天改成 uniform，由 UI 控制）----------
uniform float ambientStrength;    // 环境光强度  默认 0.35
uniform float specularStrength;   // 高光强度    默认 0.55
uniform float shininess;          // 光泽度      默认 64

void main()
{
    vec3 norm = normalize(Normal);

    // 1. 环境光
    vec3 ambient = ambientStrength * lightColor;

    // 2. 主光漫反射
    vec3  lightDir = normalize(lightPos - FragPos);
    float diff     = max(dot(norm, lightDir), 0.0);
    vec3  diffuse  = diff * lightColor;

    // 3. 补光漫反射
    vec3  fillDir     = normalize(fillLightPos - FragPos);
    float fillDiff    = max(dot(norm, fillDir), 0.0);
    vec3  fillDiffuse = fillDiff * fillLightColor;

    // 4. 镜面高光（Blinn-Phong）
    vec3 viewDir    = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec      = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    vec3 specular   = specularStrength * spec * lightColor;

    // 5. 合并（高光不乘物体颜色，保持"白点"的塑料质感）
    vec3 result = (ambient + diffuse + fillDiffuse) * objectColor + specular;

    FragColor = vec4(result, 1.0);
}
