#version 330 core

// ============================================================
// 第 18 天：半透明轨道形状的片段着色器
//
// 两个要点：
//   1. 背面也要画（glCullFace 分两次调用），所以法线要按 gl_FrontFacing 翻面，
//      否则背面永远是黑的。
//   2. 透明度不写常数，用 Fresnel：正对视线的地方最透、边缘最不透明。
//      常数 alpha 的球看起来像一块塑料片，有 Fresnel 才像"一层壳"。
// ============================================================

in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3  shapeColor;   // 形状基色（p_x 红 / p_y 绿 / p_z 蓝）
uniform float baseAlpha;    // 基础不透明度（面板滑杆）
uniform vec3  viewPos;      // 相机位置
uniform vec3  lightPos;     // 主光位置

void main()
{
    vec3 N = normalize(Normal);
    if (!gl_FrontFacing)
        N = -N;                       // 背面法线朝里，翻过来才能得到正确亮度

    vec3 V = normalize(viewPos - FragPos);

    // facing = 1 正对相机，= 0 视线与表面相切
    float facing = clamp(abs(dot(N, V)), 0.0, 1.0);
    float rim    = pow(1.0 - facing, 2.5);

    // 边缘不透明度最多抬 0.55，且封顶 0.95 —— 完全 1.0 会丢掉"透明"的观感
    float alpha = clamp(baseAlpha + rim * 0.55, 0.0, 0.95);

    // 一点朗伯光照 + 边缘提亮，给球面体积感
    float diff  = max(dot(N, normalize(lightPos - FragPos)), 0.0);
    vec3  color = shapeColor * (0.55 + 0.45 * diff) + vec3(rim * 0.30);

    FragColor = vec4(color, alpha);
}
