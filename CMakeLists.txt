cmake_minimum_required(VERSION 4.1)
project(sppc C)
set(CMAKE_C_STANDARD 23)

add_library(${PROJECT_NAME} SHARED sources/sppc2/sppc2.c)
target_include_directories(${PROJECT_NAME} PRIVATE headers)
target_compile_definitions(${PROJECT_NAME} PRIVATE
        FILE_OFFSET_BITS=64
        DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)
