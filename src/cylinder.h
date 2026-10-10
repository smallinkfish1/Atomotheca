// src/cylinder.h
// 圆柱侧面网格生成。默认沿 Y 轴，高度范围 [-0.5, +0.5]，中心在原点。
// 不生成底面（球棍模型里两端被原子球遮住）。
// 数据格式：[位置 xyz, 法线 xyz]

#ifndef CYLINDER_H
#define CYLINDER_H

#include <vector>
#include <cmath>

// 生成圆柱侧面
// 参数：
//   radius      —— 圆柱半径
//   sectorCount —— 绕圆周方向分段数
// 说明：高度固定为 1（从 -0.5 到 +0.5），实际长度靠 Model 矩阵的 Y 缩放控制
inline void generateCylinder(float radius,
                             int sectorCount,
                             std::vector<float>& outVertices,
                             std::vector<unsigned int>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    const float PI = 3.14159265358979f;

    // ---------- 1. 生成顶点 ----------
    // 每个经度位置生成上下两个顶点
    for (int i = 0; i <= sectorCount; ++i)
    {
        float phi = 2.0f * PI * (float)i / (float)sectorCount;
        float x = std::cos(phi);
        float z = std::sin(phi);

        // 侧面法线：水平向外（不含 Y 分量），因为侧面是竖直的
        float nx = x;
        float nz = z;

        // 顶面顶点（y = +0.5）
        outVertices.push_back(x * radius);   // 位置 x
        outVertices.push_back(0.5f);         // 位置 y
        outVertices.push_back(z * radius);   // 位置 z
        outVertices.push_back(nx);           // 法线 x
        outVertices.push_back(0.0f);         // 法线 y（侧面是竖直的，法线无垂直分量）
        outVertices.push_back(nz);           // 法线 z

        // 底面顶点（y = -0.5）
        outVertices.push_back(x * radius);
        outVertices.push_back(-0.5f);
        outVertices.push_back(z * radius);
        outVertices.push_back(nx);
        outVertices.push_back(0.0f);
        outVertices.push_back(nz);
    }

    // ---------- 2. 生成索引 ----------
    // 每对相邻经线构成一个长方形，切成 2 个三角形
    for (int i = 0; i < sectorCount; ++i)
    {
        // 第 i 组：上顶点 = i*2，下顶点 = i*2+1
        unsigned int top0    = i * 2;
        unsigned int bottom0 = i * 2 + 1;
        unsigned int top1    = (i + 1) * 2;
        unsigned int bottom1 = (i + 1) * 2 + 1;

        // 三角形 1
        outIndices.push_back(top0);
        outIndices.push_back(bottom0);
        outIndices.push_back(top1);

        // 三角形 2
        outIndices.push_back(top1);
        outIndices.push_back(bottom0);
        outIndices.push_back(bottom1);
    }
}

#endif // CYLINDER_H
