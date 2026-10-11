#version 330 core

// 第 9 天：与 sphere.frag 完全一致的 Blinn-Phong 光照。
// 化学键也用同一套光照，保证球和棍的质感统一。

in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 objectColor;

uniform vec3 lightPos;
uniform vec3 lightColor;

uniform vec3 fillLightPos;
uniform vec3 fillLightColor;

uniform vec3 viewPos;

void main()
{
    vec3 norm = normalize(Normal);

    // 1. 环境光
    float ambientStrength = 0.35;
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
    float specularStrength = 0.55f;
    float shininess        = 64.0f;

    vec3 viewDir    = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec      = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    vec3 specular   = specularStrength * spec * lightColor;

    // 5. 合并
    vec3 result = (ambient + diffuse + fillDiffuse) * objectColor + specular;

    FragColor = vec4(result, 1.0);
}
