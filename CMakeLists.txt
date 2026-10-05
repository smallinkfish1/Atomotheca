# CMakeLists.txt
# 第 4 天：新增 GLM 依赖
cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# 查找 GLM（apt 安装的 libglm-dev 会提供 glmConfig.cmake）
find_package(glm QUIET)

add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

add_executable(molecule_viewer
    src/main.cpp
)

target_link_libraries(molecule_viewer PRIVATE glfw OpenGL::GL glad)
target_include_directories(molecule_viewer PRIVATE
    external/glad/include
    src
)

# 如果系统找到了 GLM 包，就链接它；否则假定头文件在默认系统路径
if(glm_FOUND)
    target_link_libraries(molecule_viewer PRIVATE glm::glm)
endif()

if(MSVC)
    target_compile_options(molecule_viewer PRIVATE /W4)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()
