// src/path_utils.h
// 第 13 天：跨平台资源路径解析。
//
// 解决的问题：
//   前 12 天，着色器和分子文件都用相对路径（"src/shaders/xxx.vert"、"assets/xxx.mol"），
//   所以**必须在项目根目录运行**。一旦 cd 到 build/ 里执行，就找不到文件。
//
// 解决方案：
//   按顺序尝试三个位置，找到就用：
//     1) 当前工作目录（项目根目录运行时）
//     2) 可执行文件所在目录（从 build/ 里直接运行时，资源已被 CMake 复制过去）
//     3) 可执行文件所在目录的上一级（build/ 的父目录，即项目根）
//
// 这样无论从哪里启动，都能正确找到资源。

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

namespace pathutil
{

namespace fs = std::filesystem;

// ============================================================
// 取可执行文件所在目录
// 三个平台的做法完全不同，所以要用宏分开写
// ============================================================
inline fs::path executableDir()
{
#if defined(_WIN32)
    // Windows：GetModuleFileNameW 返回当前进程的 exe 路径
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return fs::path(buf).parent_path();

#elif defined(__APPLE__)
    // macOS：_NSGetExecutablePath 需要两段式调用（先问长度再取）
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return {};
    std::error_code ec;
    fs::path p = fs::canonical(buf, ec);   // 解析符号链接和相对路径
    return ec ? fs::path(buf).parent_path() : p.parent_path();

#else
    // Linux：读 /proc/self/exe 这个特殊符号链接
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec)
        return {};
    return p.parent_path();
#endif
}

// ============================================================
// 解析资源路径
//
// 参数 relative 可以是相对路径，也可以是绝对路径。
// 返回实际存在的路径；全都找不到时返回空字符串。
// ============================================================
inline std::string resolveResource(const std::string& relative)
{
    std::error_code ec;

    // ---------- 1. 当前工作目录 ----------
    // 这一条覆盖"在项目根目录运行"的常见情况，也处理绝对路径
    if (fs::exists(relative, ec))
        return relative;

    // ---------- 2 & 3. 可执行文件目录及其上一级 ----------
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

    return {};   // 没找到
}

} // namespace pathutil

#endif // PATH_UTILS_H
