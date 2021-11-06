cmake_minimum_required(VERSION 3.5)

project(debugger1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(debugger1 main.cpp)

target_link_libraries(debugger1 PRIVATE DbgHelp glfw3 glew32s opengl32)

target_compile_options(debugger1 PRIVATE /W4)
