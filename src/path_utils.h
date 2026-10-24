// src/path_utils.h
// 跨平台资源路径解析。
//
// 桌面（第 13 天）：
//   依次尝试「工作目录 → 可执行文件目录 → 可执行文件上级目录」
//
// Web（第 14 天）：
//   资源已经被 Emscripten 用 --preload-file 打包进虚拟文件系统，
//   路径固定就是 "assets/xxx.mol"，直接返回即可，不需要查找。

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <filesystem>
#include <string>
#include <system_error>

#ifndef __EMSCRIPTEN__
#  if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
// NOMINMAX 必须定义：否则 <windows.h> 会定义 min/max 宏，
// 把 main.cpp 里的 std::max(1.0f, app.molR) 展开成语法错误。
#    ifndef NOMINMAX
#      define NOMINMAX
#    endif
#    include <windows.h>
#  elif defined(__APPLE__)
#    include <mach-o/dyld.h>
#  else
#    include <unistd.h>
#  endif
#endif

namespace pathutil
{

namespace fs = std::filesystem;

// ============================================================
// 取可执行文件所在目录（仅桌面需要；Web 上返回空）
// ============================================================
inline fs::path executableDir()
{
#if defined(__EMSCRIPTEN__)
    return {};          // 浏览器里没有"可执行文件"这个概念
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return fs::path(buf).parent_path();

#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return {};
    std::error_code ec;
    fs::path p = fs::canonical(buf, ec);
    return ec ? fs::path(buf).parent_path() : p.parent_path();

#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec)
        return {};
    return p.parent_path();
#endif
}

// ============================================================
// 解析资源路径
// ============================================================
inline std::string resolveResource(const std::string& relative)
{
#if defined(__EMSCRIPTEN__)
    // Web：虚拟文件系统里路径是固定的，直接返回
    return relative;
#else
    std::error_code ec;

    // 1. 当前工作目录（也覆盖绝对路径）
    if (fs::exists(relative, ec))
        return relative;

    // 2 & 3. 可执行文件目录及其上一级
    fs::path exeDir = executableDir();
    if (!exeDir.empty())
    {
        fs::path beside = exeDir / relative;
        if (fs::exists(beside, ec))
            return beside.string();

        fs::path parent = exeDir.parent_path() / relative;
        if (fs::exists(parent, ec))
            return parent.string();
    }

    return {};
#endif
}

} // namespace pathutil

#endif // PATH_UTILS_H
