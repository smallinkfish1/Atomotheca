# ============================================================
#  Molecule Viewer — CMake 构建脚本
#
#  两种构建模式：
#
#    桌面（默认）
#      cmake -S . -B build && cmake --build build -j
#
#    Web（Emscripten）
#      emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
#      cmake --build build-web -j
#      → 输出 build-web/molecule_viewer.html
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.1.0 LANGUAGES C CXX)

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
    find_package(glfw3 3.3 REQUIRED)
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
add_executable(molecule_viewer src/main.cpp)

target_include_directories(molecule_viewer PRIVATE
    src
)

target_compile_definitions(molecule_viewer PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

# ------------------------------------------------------------
#  Web 构建（Emscripten）
# ------------------------------------------------------------
if(EMSCRIPTEN)

    # ---- GLM 需要单独准备 ----
    # emcc 的 sysroot 里没有 GLM，所以要自己放一份到 external/glm/
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
        # 可选：自定义页面外壳
        # --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html
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

    if(glm_FOUND)
        target_link_libraries(molecule_viewer PRIVATE glm::glm)
    endif()

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
    # path_utils.h 里也定义了一份，这里再加一道保险，双保险无副作用。
    target_compile_definitions(molecule_viewer PRIVATE NOMINMAX)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()

if(APPLE AND NOT EMSCRIPTEN)
    target_compile_definitions(molecule_viewer PRIVATE GL_SILENCE_DEPRECATION)
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
