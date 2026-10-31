#version 330 core

// 第 11 天：与 sphere.frag 完全一致的 Blinn-Phong 光照，
// 光照参数改为 uniform，可由 ImGui 面板实时调节。

in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 objectColor;

uniform vec3 lightPos;
uniform vec3 lightColor;

uniform vec3 fillLightPos;
uniform vec3 fillLightColor;

uniform vec3 viewPos;

// 光照参数（由 UI 控制）
uniform float ambientStrength;
uniform float specularStrength;
uniform float shininess;

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

    // 5. 合并
    vec3 result = (ambient + diffuse + fillDiffuse) * objectColor + specular;

    FragColor = vec4(result, 1.0);
}
