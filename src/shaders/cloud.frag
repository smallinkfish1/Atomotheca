#version 330 core

// ============================================================
// 第 20 天：电子云点云的片段着色器
//
// 两件事：
//   1. 把方形的点裁成圆形，边缘做软过渡（不用纹理，用 gl_PointCoord 算）。
//      方形点放大后是明显的小方块，一眼就露馅。
//   2. 颜色按 psi 的符号取——暖红 / 冷蓝。节面两侧符号相反，
//      所以节面在画面上会自己变成一条颜色分界，不需要另外画线。
//
// 透明度**不乘密度**：点的数量密度已经正比于 |psi|^2 了，
// 再乘一次等于把密度平方，中心会糊成死白。见 cloud.cpp 开头的说明。
// ============================================================

in float vPhase;

out vec4 FragColor;

uniform vec3  colorPositive;   // psi > 0
uniform vec3  colorNegative;   // psi < 0
uniform float opacity;         // 单个点的不透明度

void main()
{
    // gl_PointCoord 在点内是 [0,1]²，挪到 [-1,1]² 得到圆心在 0 的圆盘
    vec2  d  = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0)
        discard;                       // 圆外丢掉，方形变圆形

    float falloff = 1.0 - r2;          // 中心实、边缘淡，免得出现硬边

    vec3 color = (vPhase >= 0.0) ? colorPositive : colorNegative;

    FragColor = vec4(color, opacity * falloff);
}
