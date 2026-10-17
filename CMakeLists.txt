cmake_minimum_required(VERSION 4.1)
project(sppc C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SPPC_BUILD_TESTS "Build the tests and register them with ctest" ON)

find_package(Threads REQUIRED)

# Compiled once, into both the shared library and each test: the tests reach
# the runtime's internals, which the library does not export.
add_library(sppc_objects OBJECT
  sources/sppc/sppc.c
  headers/sppc/asm/gt_switch.S)
set_target_properties(sppc_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sppc_objects PUBLIC headers)
target_compile_definitions(sppc_objects PUBLIC
  _FILE_OFFSET_BITS=64
  DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)

# Only what the code marks "_sppc_api" / "_sppc_export" leaves the library;
# everything else, the context switch included, is hidden where it is defined.
# msvc exports nothing that is not "dllexport" in the first place.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sppc_objects PRIVATE $<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>)
endif()

add_library(${PROJECT_NAME} SHARED $<TARGET_OBJECTS:sppc_objects>)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# The same link hardening the compiler gives the executable, so the runtime it
# loads is not the one writable ".got.plt" left in the process. ELF only: these
# are GNU ld / lld options, and Mach-O and PE have their own.
set(SPPC_ELF_HARDENING OFF)
if(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  set(SPPC_ELF_HARDENING ON)
  target_link_options(${PROJECT_NAME} PRIVATE
    "LINKER:-z,relro,-z,now"
    "LINKER:-z,noexecstack"
    "LINKER:-z,separate-code"
    "LINKER:-z,defs"
    "LINKER:-z,text"
    "LINKER:--error-rwx-segments")
endif()

if(SPPC_BUILD_TESTS)
  enable_testing()

  file(GLOB SPPC_TESTS CONFIGURE_DEPENDS tests/test_*.c)
  foreach(source ${SPPC_TESTS})
    get_filename_component(name ${source} NAME_WE)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE sppc_objects Threads::Threads ${CMAKE_DL_LIBS})
    # The tests check with "assert", including calls made only inside one, so
    # a release build's NDEBUG must not compile them away.
    target_compile_options(${name} PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
    add_test(NAME ${name} COMMAND ${name})
    # Heap damage from the runtime tends to surface far from its cause.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "MALLOC_CHECK_=3" TIMEOUT 120)
  endforeach()

  # The library exports exactly the public api, on every platform.
  add_test(NAME test_exports
    COMMAND ${CMAKE_COMMAND}
      -DLIBRARY=$<TARGET_FILE:${PROJECT_NAME}>
      -DHEADERS=${CMAKE_CURRENT_SOURCE_DIR}/headers
      -DNM=${CMAKE_NM}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exports.cmake)

  # The library carries the link hardening asked for above.
  if(SPPC_ELF_HARDENING)
    add_test(NAME test_hardening
      COMMAND ${CMAKE_COMMAND}
        -DLIBRARY=$<TARGET_FILE:${PROJECT_NAME}>
        -DREADELF=${CMAKE_READELF}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_hardening.cmake)
  endif()

  # The headers stay standard C17, warning-free, with no extension creeping
  # back in. gcc and clang only: msvc has no equivalent of -pedantic-errors.
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_test(NAME test_strict_c17
      COMMAND ${CMAKE_C_COMPILER} -std=c17 -pedantic-errors -Wall -Wextra -Wvla -Werror
        -fsyntax-only -I${CMAKE_CURRENT_SOURCE_DIR}/headers
        ${CMAKE_CURRENT_SOURCE_DIR}/sources/sppc/sppc.c)
  endif()
endif()
