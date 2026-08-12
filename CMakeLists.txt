cmake_minimum_required(VERSION 4.1)
project(sppc C ASM)
set(CMAKE_C_STANDARD 23)

add_library(${PROJECT_NAME} SHARED
  sources/sppc/sppc.c
  headers/sppc/asm/gt_switch.S)

target_include_directories(${PROJECT_NAME} PRIVATE headers)
target_compile_features(${PROJECT_NAME} PRIVATE c_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  _FILE_OFFSET_BITS=64
  DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)

# Only sppc_* leaves the shared library. The version script is authoritative
# (it also covers gt_switch, which comes from assembly); -fvisibility=hidden
# just lets the compiler skip PLT indirection for internal calls.
target_compile_options(${PROJECT_NAME} PRIVATE -fvisibility=hidden)
target_link_options(${PROJECT_NAME} PRIVATE
  "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/sppc.map")
set_target_properties(${PROJECT_NAME} PROPERTIES
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/sppc.map)
