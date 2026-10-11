// src/sphere.h
// 球体网格生成：用经纬（UV）法把球面切成很多小三角形。
// 生成的数据格式：[位置 xyz, 法线 xyz]，共 6 个 float 一组。

#ifndef SPHERE_H
#define SPHERE_H

#include <vector>
#include <cmath>

// 一个顶点：位置 + 法线
struct SphereVertex
{
    float px, py, pz;   // 位置
    float nx, ny, nz;   // 法线（球心在原点时，法线 = 归一化后的位置）
};

// 生成球体顶点和索引
// 参数：
//   radius      —— 球半径
//   sectorCount —— 经度方向分段数（绕一圈分几份）
//   stackCount  —— 纬度方向分层数（南北极之间分几层）
// 输出：
//   outVertices —— 顶点数组（每个顶点 6 个 float：pos + normal）
//   outIndices  —— 索引数组
inline void generateSphere(float radius,
                           int sectorCount,
                           int stackCount,
                           std::vector<float>& outVertices,
                           std::vector<unsigned int>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    // 预分配，避免反复扩容
    outVertices.reserve((sectorCount + 1) * (stackCount + 1) * 6);

    const float PI = 3.14159265358979f;

    // ---------- 1. 生成顶点 ----------
    // i 控制纬度（从北极 0 到南极 stackCount）
    // j 控制经度（绕一圈 0 到 sectorCount）
    for (int i = 0; i <= stackCount; ++i)
    {
        // theta：从北极量起的极角，范围 [0, PI]
        float theta = PI * (float)i / (float)stackCount;
        float sinTheta = std::sin(theta);
        float cosTheta = std::cos(theta);

        for (int j = 0; j <= sectorCount; ++j)
        {
            // phi：经度角，范围 [0, 2*PI]
            float phi = 2.0f * PI * (float)j / (float)sectorCount;
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            // 球面坐标 → 直角坐标
            float x = sinTheta * cosPhi;
            float y = cosTheta;
            float z = sinTheta * sinPhi;

            // 位置
            outVertices.push_back(x * radius);
            outVertices.push_back(y * radius);
            outVertices.push_back(z * radius);

            // 法线：球心在原点时，法线就是归一化后的位置
            // (x, y, z) 本身已经是单位向量，因为它来自 sin/cos
            outVertices.push_back(x);
            outVertices.push_back(y);
            outVertices.push_back(z);
        }
    }

    // ---------- 2. 生成索引 ----------
    // 每个"小格子"由相邻两层（i, i+1）和相邻两列（j, j+1）的 4 个顶点组成
    for (int i = 0; i < stackCount; ++i)
    {
        for (int j = 0; j < sectorCount; ++j)
        {
            // 当前格子四个顶点的索引
            unsigned int k1 = i       * (sectorCount + 1) + j;
            unsigned int k2 = (i + 1) * (sectorCount + 1) + j;

            // 切成两个三角形。
            // ★ 绕序必须为"从球外看逆时针"（CCW），才能配合 glFrontFace(GL_CCW)
            //   和背面剔除正常工作。
            //   如果写反了，被剔除的会是"朝外的面"，于是我们只能看到球的
            //   内壁（远侧表面），导致：
            //     1) 球体没有任何明暗渐变和高光（内壁法线背对相机）
            //     2) 深度关系错乱，后面的物体会"穿透"到前面来
            // 三角形 1：k1 → k1+1 → k2
            outIndices.push_back(k1);
            outIndices.push_back(k1 + 1);
            outIndices.push_back(k2);

            // 三角形 2：k1+1 → k2+1 → k2
            outIndices.push_back(k1 + 1);
            outIndices.push_back(k2 + 1);
            outIndices.push_back(k2);
        }
    }
}

#endif // SPHERE_H
