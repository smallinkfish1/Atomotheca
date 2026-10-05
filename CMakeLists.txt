# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

add_executable(molecule_viewer
    src/main.cpp
)

target_link_libraries(molecule_viewer PRIVATE glfw OpenGL::GL glad)
target_include_directories(molecule_viewer PRIVATE
    external/glad/include
    src                        # 让 #include "shader.h" 能找到
)

if(MSVC)
    target_compile_options(molecule_viewer PRIVATE /W4)
else()
    target_compile_options(molecule_viewer PRIVATE -Wall -Wextra)
endif()
