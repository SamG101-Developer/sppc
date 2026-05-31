cmake_minimum_required(VERSION 4.1)
project(sppc C)
set(CMAKE_C_STANDARD 23)

add_library(${PROJECT_NAME} SHARED sources/sppc/sppc.c)
target_include_directories(${PROJECT_NAME} PRIVATE headers)
target_compile_definitions(${PROJECT_NAME} PRIVATE FILE_OFFSET_BITS=64)
