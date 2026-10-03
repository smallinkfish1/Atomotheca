cmake_minimum_required(VERSION 3.16)

project(MoleculeViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(glfw3 3.3 REQUIRED)

find_package(OpenGL REQUIRED)

add_executable(molecule_viewer
    src/main.cpp
)

target_link_libraries(molecule_viewer PRIVATE glfw OpenGL::GL)
