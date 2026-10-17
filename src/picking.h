// src/picking.h
// 第 12 天：鼠标拾取（Ray Picking）。
//
// 核心思路：把"屏幕上的一个像素"反算成"三维空间里的一条射线"，
// 然后判断这条射线打中了哪个原子的球体，取最近的那个。
//
// 这是第 4 天学的"投影矩阵"的逆运算 —— 那次是 3D → 2D，这次是 2D → 3D。

#ifndef PICKING_H
#define PICKING_H

#include <glm/glm.hpp>

#include <cmath>

namespace picking
{

// 一条射线：起点 + 单位方向
struct Ray
{
    glm::vec3 origin;
    glm::vec3 dir;      // 必须是单位向量
};

// ============================================================
// 屏幕坐标 → 世界空间射线
//
// 参数：
//   screenX / screenY —— 鼠标位置，原点在窗口左上角，单位像素
//   screenW / screenH —— 窗口尺寸
//   view / projection —— 当前相机矩阵
// ============================================================
inline Ray makeRay(float screenX, float screenY,
                   float screenW, float screenH,
                   const glm::mat4& view,
                   const glm::mat4& projection)
{
    // ---------- 1. 屏幕坐标 → NDC（范围 [-1, 1]）----------
    float ndcX = (2.0f * screenX) / screenW - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY) / screenH;   // ★ y 轴要翻转

    // ---------- 2. 在裁剪空间取近、远两个点 ----------
    // 近平面 z = -1，远平面 z = +1（OpenGL 的 NDC 约定）
    glm::vec4 nearClip(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farClip (ndcX, ndcY,  1.0f, 1.0f);

    // ---------- 3. 逆投影 → 观察空间 ----------
    glm::mat4 invProj = glm::inverse(projection);
    glm::vec4 nearEye = invProj * nearClip;
    glm::vec4 farEye  = invProj * farClip;

    // ★★ 关键一步：透视除法
    //    投影矩阵输出的 w 分量不是 1（它就是透视变换藏身的地方），
    //    必须先除以 w 才能得到真正的坐标。
    //    漏了这一步，射线方向会完全错乱。
    nearEye /= nearEye.w;
    farEye  /= farEye.w;

    // ---------- 4. 逆视图 → 世界空间 ----------
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 nearWorld = glm::vec3(invView * nearEye);
    glm::vec3 farWorld  = glm::vec3(invView * farEye);

    Ray ray;
    ray.origin = nearWorld;                                  // 近平面上的点
    ray.dir    = glm::normalize(farWorld - nearWorld);       // 指向远平面
    return ray;
}

// ============================================================
// 射线与球体求交
//
// 返回最近正交点对应的 t（沿射线的距离）；不相交返回 -1。
//
// 数学推导：
//   球心 C，半径 r；射线 P(t) = O + t·D（D 为单位向量）
//   相交条件：|P(t) - C|² = r²
//   令 oc = O - C，展开得：
//       t² (D·D) + 2t (D·oc) + (oc·oc - r²) = 0
//   因为 D·D = 1，这是一个标准二次方程：
//       t² + 2bt + c = 0,  b = D·oc,  c = oc·oc - r²
//   判别式 disc = b² - c
// ============================================================
inline float raySphere(const Ray& ray, const glm::vec3& center, float radius)
{
    glm::vec3 oc = ray.origin - center;

    float b = glm::dot(oc, ray.dir);
    float c = glm::dot(oc, oc) - radius * radius;

    float disc = b * b - c;
    if (disc < 0.0f)                 // 射线没碰到球
        return -1.0f;

    float sq = std::sqrt(disc);

    float t0 = -b - sq;              // 近交点
    if (t0 > 0.0f) return t0;

    float t1 = -b + sq;              // 远交点（相机在球内部时才会用到）
    if (t1 > 0.0f) return t1;

    return -1.0f;                    // 两个交点都在背后
}

// ============================================================
// 世界坐标 → 屏幕坐标（用于画标签）
//
// 返回 false 表示这个点在相机背后，不应该显示。
// ============================================================
inline bool worldToScreen(const glm::vec3& world,
                          const glm::mat4& view,
                          const glm::mat4& projection,
                          float screenW, float screenH,
                          float& outX, float& outY)
{
    glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);

    // w <= 0 表示点在相机背后（投影后会被"翻转"，必须剔除）
    if (clip.w <= 0.0f)
        return false;

    glm::vec3 ndc = glm::vec3(clip) / clip.w;

    outX = (ndc.x * 0.5f + 0.5f) * screenW;
    outY = (1.0f - (ndc.y * 0.5f + 0.5f)) * screenH;
    return true;
}

} // namespace picking

#endif // PICKING_H
