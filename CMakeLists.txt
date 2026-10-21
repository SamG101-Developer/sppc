cmake_minimum_required(VERSION 4.1)
project(sppc C ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SPPC_BUILD_TESTS "Build the tests and register them with ctest" ON)

find_package(Threads REQUIRED)

# Compiled once, into both the shared library and each test:
# the tests reach the runtime's internals, which the library
# does not export.
set(SPPC_SOURCES
  sources/sppc/sppc.c
  headers/sppc/asm/gt_switch.S)
add_library(
  sppc_objects OBJECT ${SPPC_SOURCES})
set_target_properties(
  sppc_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(
  sppc_objects PUBLIC headers)
target_compile_definitions(
  sppc_objects PUBLIC
  _FILE_OFFSET_BITS=64
  DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)

# Only what the code marks "_sppc_api" / "_sppc_export" leaves
# the library; everything else, the context switch included,
# is hidden where it is defined. msvc exports nothing that is
# not "dllexport" in the first place.
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    sppc_objects PRIVATE $<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>)
endif ()

# Fortified libc calls ("__memcpy_chk" and friends). The compiler
# fails a link against an ffi library without them (this is flag
# adjustable in the s++ compiler).
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    sppc_objects PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<NOT:$<CONFIG:Debug>>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=$<IF:$<PLATFORM_ID:Darwin>,2,3>>")
endif ()

add_library(
  ${PROJECT_NAME} SHARED $<TARGET_OBJECTS:sppc_objects>)
target_link_libraries(
  ${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# The same link hardening the compiler gives the executable,
# so the runtime it loads is not the one writable ".got.plt"
# left in the process. ELF only: these are GNU ld / lld
# options, and Mach-O and PE have their own.
if (UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_link_options(${PROJECT_NAME} PRIVATE
    "LINKER:-z,relro,-z,now"
    "LINKER:-z,noexecstack"
    "LINKER:-z,separate-code"
    "LINKER:-z,defs"
    "LINKER:-z,text"
    "LINKER:--error-rwx-segments")
endif ()

if (SPPC_BUILD_TESTS)
  enable_testing()

  # Every test also runs against the blocking i/o backend, the
  # one a platform without its own starts on, where the ring
  # would otherwise hide it. Only where there is a ring to
  # compare with: elsewhere it is the only backend.
  set(SPPC_TEST_BACKENDS default)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(
      sppc_objects_blocking OBJECT ${SPPC_SOURCES})
    set_target_properties(
      sppc_objects_blocking PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(
      sppc_objects_blocking PUBLIC headers)
    target_compile_definitions(
      sppc_objects_blocking PUBLIC
      $<TARGET_PROPERTY:sppc_objects,INTERFACE_COMPILE_DEFINITIONS>
      SPPC_ASYNC_BLOCKING=1)
    list(APPEND SPPC_TEST_BACKENDS blocking)
  endif ()

  file(GLOB SPPC_TESTS CONFIGURE_DEPENDS tests/test_*.c)
  foreach (source ${SPPC_TESTS})
    get_filename_component(base ${source} NAME_WE)
    foreach (backend ${SPPC_TEST_BACKENDS})
      if (backend STREQUAL "default")
        set(name ${base})
        set(objects sppc_objects)
      else ()
        set(name ${base}_${backend})
        set(objects sppc_objects_${backend})
      endif ()
      add_executable(${name} ${source})
      target_link_libraries(
        ${name} PRIVATE ${objects} Threads::Threads ${CMAKE_DL_LIBS})

      # The tests check with "assert", including calls made only
      # inside one, so a release build's NDEBUG must not compile
      # them away.
      target_compile_options(
        ${name} PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
      add_test(
        NAME ${name} COMMAND ${name})

      # Heap damage from the runtime tends to surface far from its
      # cause. 77 is a test reporting it cannot run here (no ring
      # for example), shown as skipped.
      set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "MALLOC_CHECK_=3" TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach ()
  endforeach ()

  # The headers stay standard C17, warning-free, with no extension
  # creeping back in. gcc and clang only: msvc has no equivalent
  # of -pedantic-errors.
  if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_test(NAME test_strict_c17
      COMMAND ${CMAKE_C_COMPILER} -std=c17 -pedantic-errors -Wall -Wextra -Wvla -Werror
      -fsyntax-only -I${CMAKE_CURRENT_SOURCE_DIR}/headers
      ${CMAKE_CURRENT_SOURCE_DIR}/sources/sppc/sppc.c)
  endif ()
endif ()
