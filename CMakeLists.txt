# ============================================================
#  Molecule Viewer — CMake 构建脚本
#
#  第 13 天：整理为跨平台版本
#    - Windows / Linux / macOS 三平台适配
#    - 构建后自动把 assets/ 和 src/shaders/ 复制到可执行文件旁边
#      （这样从 build/ 目录直接运行也能找到资源）
#    - 版本号通过编译宏注入，程序里可以用 APP_VERSION
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.13.0 LANGUAGES C CXX)

# ---------- 语言标准 ----------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)      # 关闭 GNU 扩展，保证可移植

# ---------- 默认构建类型 ----------
# 多配置生成器（Visual Studio、Xcode）不需要这个设置
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------- 依赖 ----------
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm QUIET)            # 有的系统有 glmConfig.cmake，有的只有头文件

# ============================================================
#  GLAD（随仓库提供，无需下载）
# ============================================================
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
# Linux 上 GLAD 内部用 dlopen 找函数，需要链接 dl；Windows 上 CMAKE_DL_LIBS 为空
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# ============================================================
#  Dear ImGui（需自行 clone，不在仓库中）
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
        "\n"
        "  或从 https://github.com/ocornut/imgui/releases 下载 zip，\n"
        "  解压后把文件夹重命名为 imgui，放到 external/ 下。\n"
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
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ============================================================
#  主程序
# ============================================================
add_executable(molecule_viewer
    src/main.cpp
)

target_link_libraries(molecule_viewer PRIVATE
    glfw
    OpenGL::GL
    glad
    imgui
)

target_include_directories(molecule_viewer PRIVATE
    external/glad/include
    src
)

# GLM：能找到包就链接，找不到就假定头文件在系统默认路径
if(glm_FOUND)
    target_link_libraries(molecule_viewer PRIVATE glm::glm)
endif()

# ---------- 版本号注入 ----------
target_compile_definitions(molecule_viewer PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

# ============================================================
#  ★ 构建后复制资源
#
#  目的：让 build/molecule_viewer 在 build/ 目录下直接运行也能找到
#        着色器和分子文件，不再强制"必须在项目根目录运行"。
#
#  复制后的 build/ 目录结构：
#      build/molecule_viewer
#      build/assets/*.mol, *.pdb
#      build/src/shaders/*.vert, *.frag
# ============================================================
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

# ============================================================
#  安装规则（可选）
#
#  安装后保持相对关系，可执行文件旁边就有 assets/：
#      <prefix>/molecule_viewer
#      <prefix>/assets/
#      <prefix>/src/shaders/
# ============================================================
install(TARGETS molecule_viewer RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
install(DIRECTORY src/shaders DESTINATION src)

# ============================================================
#  编译器警告与平台适配
# ============================================================
if(MSVC)
    # /utf-8：源码含中文注释，MSVC 默认按本地代码页解析会报错
    target_compile_options(molecule_viewer PRIVATE /W4 /utf-8)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    # macOS 的 OpenGL 已标记废弃，屏蔽相关警告
    target_compile_definitions(molecule_viewer PRIVATE GL_SILENCE_DEPRECATION)
endif()

# GCC 9.1 之前，std::filesystem 需要单独链接 stdc++fs
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    target_link_libraries(molecule_viewer PRIVATE stdc++fs)
endif()

# ============================================================
#  配置阶段打印构建信息
# ============================================================
message(STATUS "----------------------------------------------------")
message(STATUS " Molecule Viewer v${PROJECT_VERSION}")
message(STATUS " Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS " Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " System     : ${CMAKE_SYSTEM_NAME}")
message(STATUS "----------------------------------------------------")
