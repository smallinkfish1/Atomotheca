# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.2.0 LANGUAGES C CXX)  # 注意：加入了 C 语言（glad.c 是 C）

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# 把 GLAD 的源码和头文件加进来
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})  # GLAD 内部用 dlopen，需要 dl 库

add_executable(molecule_viewer
    src/main.cpp
)

# 链接：GLFW + OpenGL + GLAD
target_link_libraries(molecule_viewer PRIVATE glfw OpenGL::GL glad)
target_include_directories(molecule_viewer PRIVATE external/glad/include)

if(MSVC)
    target_compile_options(molecule_viewer PRIVATE /W4)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()
