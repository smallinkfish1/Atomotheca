# ============================================================
#  Molecule Viewer — CMake 构建脚本
#
#  支持的编译器 / 平台：
#    Linux    g++            （系统 glfw3 + glm）
#    Windows  MSVC           （vcpkg: glfw3 / glm）
#    Windows  MinGW-w64      （external/glfw 源码静态编译）
#    Web      Emscripten     （-sUSE_GLFW=3）
#
#  依赖查找顺序（桌面）：
#    GLFW : external/glfw 源码  →  系统 find_package(glfw3)
#    GLM  : 系统 find_package(glm)  →  external/glm 头文件
#
#  MinGW 注意事项：
#    必须让 GLFW 从源码编译（而不是链接 Linux 上的 .so），
#    并且整个 exe 静态链接 libgcc / libstdc++ / winpthread，
#    否则拷到没装 MinGW 运行时的 Windows 上会提示缺少 DLL。
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============ 依赖 ============
# Emscripten 环境下 GLFW 和 GL 由 Emscripten 自己提供（-sUSE_GLFW=3），
# 不需要 find_package，也找不到。
if(NOT EMSCRIPTEN)

    # --------------------------------------------------------
    #  GLFW 的选择顺序
    #
    #  1. 原生构建（Linux / macOS / MSVC）→ 优先系统库
    #  2. 交叉编译（MinGW）或系统库没找到 → external/glfw 源码编译
    #
    #  为什么原生构建不再优先源码：
    #    GLFW 3.4 的 X11 后端在配置阶段就 hard require 了四个开发包：
    #      libxinerama-dev  libxcursor-dev  libxi-dev  libxrandr-dev
    #    只装 libx11-dev 的机器会直接死在
    #      "Xinerama headers not found; install libxinerama development package"
    #    而 apt 装的 libglfw3-dev 已经把这些依赖编进去了，直接用最省事。
    #
    #  为什么 MinGW 必须用源码：
    #    系统里没有 Windows 版的 GLFW，交叉编译只能自己编静态库。
    #
    #  历史坑（务必别再犯）：
    #    这段代码曾无条件 set(GLFW_BUILD_X11 OFF) / (WAYLAND OFF)，
    #    本意是加快交叉编译的配置。结果 Linux 原生构建编出来的 GLFW
    #    只剩 null 后端 —— 能编译、能链接，运行时 glfwInit() 立刻失败，
    #    报错只有一句"GLFW 初始化失败"，看不出任何原因。
    # --------------------------------------------------------
    if(NOT CMAKE_CROSSCOMPILING)
        find_package(glfw3 3.3 QUIET)
    endif()

    if(glfw3_FOUND AND NOT CMAKE_CROSSCOMPILING)

        message(STATUS "  GLFW       : 系统库 ${glfw3_VERSION}")

    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/CMakeLists.txt)

        set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

        # 目标不是 Windows 时才允许编 X11 / Wayland 后端；
        # 交叉编译 Windows 时这两个后端毫无意义，关掉能省配置时间。
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_X11     OFF CACHE BOOL "" FORCE)
        else()
            # 必须显式清缓存：旧版本用 FORCE 把 OFF 写进了 CMakeCache.txt，
            # 单纯"不再 set"是没用的，缓存里的 OFF 会一直生效。
            unset(GLFW_BUILD_WAYLAND CACHE)
            unset(GLFW_BUILD_X11     CACHE)
        endif()

        add_subdirectory(external/glfw EXCLUDE_FROM_ALL)
        message(STATUS "  GLFW       : 源码编译 (external/glfw)")

    else()

        # 走到这里说明系统没有、源码也没有，让 find_package 给出标准报错
        find_package(glfw3 3.3 REQUIRED)

    endif()

    find_package(OpenGL REQUIRED)
endif()

find_package(glm QUIET)

# ============================================================
#  GLAD（仅桌面；Web 用 Emscripten 自带的 GLES3）
# ============================================================
if(NOT EMSCRIPTEN)
    add_library(glad STATIC external/glad/src/glad.c)
    target_include_directories(glad PUBLIC external/glad/include)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
endif()

# ============================================================
#  Dear ImGui
# ============================================================
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
    message(FATAL_ERROR
        "\n========================================================\n"
        "  找不到 Dear ImGui 源码。\n"
        "\n"
        "  请在项目根目录执行：\n"
        "\n"
        "    git clone --depth 1 --branch v1.90.9 \\\n"
        "      https://github.com/ocornut/imgui.git external/imgui\n"
        "========================================================\n")
endif()

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

if(NOT EMSCRIPTEN)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
endif()

# ============================================================
#  主程序
# ============================================================
add_executable(molecule_viewer
    src/main.cpp
    src/orbital/element.cpp      # 第 15 天：元素数据库（数据层，不碰 OpenGL）
    src/orbital/shape.cpp        # 第 18 天：s / p 轨道网格（纯数学，不碰 OpenGL）
    src/ui/periodic_table.cpp    # 第 16 天：周期表面板（只依赖 ImGui + 数据层）
    src/ui/energy_diagram.cpp    # 第 17 天：能级图（同上）
    src/ui/orbital_panel.cpp     # 第 18 天：轨道形状控制项（同上）
)

target_include_directories(molecule_viewer PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/external/json   # nlohmann/json 单头文件
)

target_compile_definitions(molecule_viewer PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

# ------------------------------------------------------------
#  GLM（header-only，三种来源）
# ------------------------------------------------------------
if(EMSCRIPTEN)

    # emcc 的 sysroot 里没有 GLM，必须自己放一份到 external/glm/
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/glm/glm/glm.hpp)
        message(FATAL_ERROR
            "\n========================================================\n"
            "  Web 构建需要 GLM 头文件（emcc 的 sysroot 里没有）。\n"
            "\n"
            "  请在项目根目录执行：\n"
            "\n"
            "    git clone --depth 1 --branch 1.0.1 \\\n"
            "      https://github.com/g-truc/glm.git external/glm\n"
            "========================================================\n")
    endif()

    target_include_directories(molecule_viewer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glm
    )

elseif(glm_FOUND)

    target_link_libraries(molecule_viewer PRIVATE glm::glm)

elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/glm/glm/glm.hpp)

    # 系统没装 GLM（或交叉编译时找到的是主机版），用项目里的一份
    target_include_directories(molecule_viewer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/glm
    )
    message(STATUS "  GLM        : external/glm")

else()

    message(FATAL_ERROR
        "\n========================================================\n"
        "  找不到 GLM。任选一种方式：\n"
        "\n"
        "    Ubuntu/Debian : sudo apt install libglm-dev\n"
        "    源码          : git clone --depth 1 --branch 1.0.1 \\\n"
        "                      https://github.com/g-truc/glm.git external/glm\n"
        "========================================================\n")

endif()

# ------------------------------------------------------------
#  Web 构建（Emscripten）
# ------------------------------------------------------------
if(EMSCRIPTEN)

    set_target_properties(molecule_viewer PROPERTIES
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    target_link_libraries(molecule_viewer PRIVATE imgui)

    target_link_options(molecule_viewer PRIVATE
        # GLFW 3 移植（Emscripten 提供）
        -sUSE_GLFW=3
        # 强制 WebGL2，才能用 GLSL ES 3.00
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        # 允许内存增长（加载大分子时需要）
        -sALLOW_MEMORY_GROWTH=1
        # 把资源打进虚拟文件系统
        --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@assets
        --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders@src/shaders
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_link_options(molecule_viewer PRIVATE -O3)
    endif()

    message(STATUS "  Target     : WebAssembly (WebGL2)")

# ------------------------------------------------------------
#  桌面构建
# ------------------------------------------------------------
else()

    target_link_libraries(molecule_viewer PRIVATE
        glfw
        OpenGL::GL
        glad
        imgui
    )

    # Windows 的"打开文件"对话框（GetOpenFileNameW）在 comdlg32 里
    if(WIN32)
        target_link_libraries(molecule_viewer PRIVATE comdlg32)
    endif()

    target_include_directories(molecule_viewer PRIVATE
        external/glad/include
    )

    # 构建后把资源复制到可执行文件旁边
    add_custom_command(TARGET molecule_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets
                $<TARGET_FILE_DIR:molecule_viewer>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders
                $<TARGET_FILE_DIR:molecule_viewer>/src/shaders
        COMMENT "复制 assets 和 shaders 到构建目录"
        VERBATIM
    )

    install(TARGETS molecule_viewer RUNTIME DESTINATION .)
    install(DIRECTORY assets DESTINATION .)
    install(DIRECTORY src/shaders DESTINATION src)

    message(STATUS "  Target     : Desktop (OpenGL 3.3)")
endif()

# ============================================================
#  编译选项
# ============================================================
if(MSVC)
    target_compile_options(molecule_viewer PRIVATE /W4 /utf-8)

    # NOMINMAX：禁止 <windows.h> 定义 min/max 宏。
    # 否则 main.cpp 里的 std::max(1.0f, app.molR) 会被宏展开成语法错误
    # （报 C2589 / "illegal token on right side of '::'"）。
    target_compile_definitions(molecule_viewer PRIVATE NOMINMAX)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()

if(APPLE AND NOT EMSCRIPTEN)
    target_compile_definitions(molecule_viewer PRIVATE GL_SILENCE_DEPRECATION)
endif()

# ============================================================
#  MinGW-w64（Windows 上的 GCC，或在 Linux 上交叉编译）
# ============================================================
if(WIN32 AND NOT MSVC AND NOT EMSCRIPTEN)

    # MinGW 的 <windows.h> 同样会定义 min/max 宏；NOMINMAX 必须加。
    # _WIN32_WINNT=0x0601：目标 Windows 7 及以上，GetOpenFileNameW 等 API 可用。
    target_compile_definitions(molecule_viewer PRIVATE
        NOMINMAX
        _WIN32_WINNT=0x0601
    )

    # 关键：把 GCC 运行时静态链进 exe。
    # 不加这三个参数，生成的 exe 会依赖：
    #   libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll
    # 普通 Windows 机器上没有这些 DLL，双击就报"缺少 DLL"。
    target_link_options(molecule_viewer PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )

    message(STATUS "  Runtime    : MinGW 静态运行时")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1
   AND NOT EMSCRIPTEN)
    target_link_libraries(molecule_viewer PRIVATE stdc++fs)
endif()

# ============================================================
message(STATUS "----------------------------------------------------")
message(STATUS " Molecule Viewer v${PROJECT_VERSION}")
message(STATUS " Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS " Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " System     : ${CMAKE_SYSTEM_NAME}")
message(STATUS "----------------------------------------------------")
