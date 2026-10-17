# CMakeLists.txt
# 第 11 天：新增 Dear ImGui
cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.11.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm QUIET)

# ==================== GLAD ====================
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# ==================== Dear ImGui ====================
# ImGui 的源码不随本包提供（文件较多），需要你自己下载：
#   cd 项目根目录
#   git clone --depth 1 --branch v1.90.9 https://github.com/ocornut/imgui.git external/imgui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
    message(FATAL_ERROR
        "\n========================================================\n"
        "  找不到 Dear ImGui 源码。\n"
        "\n"
        "  请在项目根目录执行下面这条命令：\n"
        "\n"
        "    git clone --depth 1 --branch v1.90.9 \\\n"
        "      https://github.com/ocornut/imgui.git external/imgui\n"
        "\n"
        "  如果 git 太慢，也可以浏览器打开下面地址下载 zip，\n"
        "  解压后把文件夹重命名为 imgui 放到 external/ 下：\n"
        "    https://github.com/ocornut/imgui/releases\n"
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

# ==================== 主程序 ====================
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

if(glm_FOUND)
    target_link_libraries(molecule_viewer PRIVATE glm::glm)
endif()

# ImGui 编译时会有一些无害的警告，这里只对我们的代码开满警告
if(MSVC)
    target_compile_options(molecule_viewer PRIVATE /W4)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()
