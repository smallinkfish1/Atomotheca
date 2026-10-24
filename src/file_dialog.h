// src/file_dialog.h
// 跨平台"选一个分子文件"的对话框。
//
// 设计原则：不引入任何第三方库。
//   Linux   —— 调用 zenity（GNOME）或 kdialog（KDE），都是桌面自带的系统程序
//   Windows —— 调用系统自带的 comdlg32（GetOpenFileNameW）
//   macOS   —— 调用系统自带的 osascript
//   Web     —— 不支持，返回空串
//
// 用法：
//     std::string path = filedialog::openMoleculeDialog();
//     if (!path.empty()) loadMoleculeFile(path);       // 用户选了文件
//     else if (!filedialog::lastError().empty())       // 出错了（不是取消）
//         std::cerr << filedialog::lastError() << std::endl;

#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <string>
#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <commdlg.h>
#endif

#if !defined(_WIN32)
#  include <array>
#  include <cstdio>
#  include <cstdlib>
#  include <sys/wait.h>
#endif

namespace filedialog {

// ---------- 错误信息（"用户取消"不算错误，这里只记录真正的失败）----------
inline std::string& lastErrorRef()
{
    static std::string msg;
    return msg;
}

inline std::string lastError()
{
    return lastErrorRef();
}

// ============================================================
//  Windows
// ============================================================
#if defined(_WIN32)

inline std::string wideToUtf8(const wchar_t* w)
{
    if (w == nullptr || w[0] == L'\0')
        return std::string();

    // WideCharToMultiByte 是 8 个参数（后两个是默认字符与使用标记）
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return std::string();

    // need 已包含结尾的 '\0'，所以按 need 分配，写完再把 '\0' 去掉
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), need, nullptr, nullptr);
    out.pop_back();
    return out;
}

inline std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();

    // 注意：MultiByteToWideChar 只有 6 个参数，
    // 比 WideCharToMultiByte 少两个（没有默认字符相关参数）。
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (need <= 0)
        return std::wstring();

    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), need);
    out.pop_back();
    return out;
}

inline std::string openMoleculeDialog(const std::string& title = "Open molecule")
{
    lastErrorRef().clear();

    wchar_t fileBuf[MAX_PATH] = { 0 };

    const std::wstring wtitle = utf8ToWide(title);

    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    // 注意：这里的字符串是宽字符字面量，\0 分隔"显示名"和"通配符"
    ofn.lpstrFilter = L"Molecule files (*.mol;*.pdb)\0*.mol;*.pdb\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.lpstrDefExt = L"mol";
    // OFN_NOCHANGEDIR 很关键：
    // 不加的话，对话框会把进程的"当前工作目录"改成你选文件的那个目录，
    // 之后的相对路径（assets/、src/shaders/）就全找不到了。
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
    {
        // 用户点了取消，或者关闭了窗口 —— 不是错误
        return std::string();
    }

    return wideToUtf8(fileBuf);
}

// ============================================================
//  Web：不支持
// ============================================================
#elif defined(__EMSCRIPTEN__)

inline std::string openMoleculeDialog(const std::string& = "")
{
    lastErrorRef() = "网页版暂不支持文件对话框";
    return std::string();
}

// ============================================================
//  Linux / macOS
// ============================================================
#else

// 去掉首尾空白（zenity 输出末尾带换行）
inline std::string trimCopy(const std::string& s)
{
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return std::string();
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// 只用于标题，清掉会破坏 shell 引号语法的字符
inline std::string sanitizeForShell(const std::string& s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s)
    {
        if (c != '"' && c != '\'' && c != '`' && c != '$')
            r += c;
    }
    return r;
}

// 检查系统里有没有这个命令
inline bool commandExists(const char* cmd)
{
    std::string line = std::string("command -v ") + cmd + " >/dev/null 2>&1";
    return std::system(line.c_str()) == 0;
}

// 执行命令并抓取标准输出。
// 返回子进程退出码；返回 -1 表示没能启动或状态异常。
//   zenity / kdialog 的约定：0 = 选中文件，1 = 用户取消
inline int runCapture(const std::string& cmd, std::string& out)
{
    out.clear();

    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr)
        return -1;

    std::array<char, 512> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        out += buf.data();

    int status = pclose(pipe);
    if (status == -1)
        return -1;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return -1;
}

#if defined(__APPLE__)

inline std::string openMoleculeDialog(const std::string& title = "Open molecule")
{
    lastErrorRef().clear();

    const std::string cmd =
        "/usr/bin/osascript -e 'POSIX path of (choose file with prompt \"" +
        sanitizeForShell(title) + "\" of type {\"mol\", \"pdb\"})' 2>/dev/null";

    std::string out;
    int code = runCapture(cmd, out);

    if (code == 0)
        return trimCopy(out);

    if (code != 1)
        lastErrorRef() = "文件对话框启动失败（退出码 " + std::to_string(code) +
                         "），请检查图形环境是否正常。";

    return std::string();
}

#else  // Linux

inline std::string openMoleculeDialog(const std::string& title = "Open molecule")
{
    lastErrorRef().clear();

    const std::string safeTitle = sanitizeForShell(title);

    std::string cmd;

    if (commandExists("zenity"))
    {
        cmd = "zenity --file-selection --title=\"" + safeTitle + "\" "
              "--file-filter=\"Molecule files | *.mol *.pdb\" "
              "--file-filter=\"All files | *\" 2>/dev/null";
    }
    else if (commandExists("kdialog"))
    {
        cmd = "kdialog --getopenfilename . \"*.mol *.pdb|Molecule files\" "
              "--title \"" + safeTitle + "\" 2>/dev/null";
    }
    else
    {
        lastErrorRef() = "系统里没有 zenity 或 kdialog，无法弹出文件对话框。"
                         "请安装其中一个：sudo apt install zenity";
        return std::string();
    }

    std::string out;
    int code = runCapture(cmd, out);

    if (code == 0)
        return trimCopy(out);

    if (code != 1)
        lastErrorRef() = "文件对话框启动失败（退出码 " + std::to_string(code) +
                         "），请确认图形环境正常。";

    return std::string();
}

#endif // __APPLE__

#endif // 平台分支

} // namespace filedialog

#endif // FILE_DIALOG_H
