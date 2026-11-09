cmake_minimum_required(VERSION 3.31)
project(sppc C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SPPC_BUILD_TESTS "Build the tests and register them with ctest" ON)

include(cmake/Dependencies.cmake)

# Compiled once, into both the shared library and each test:
# the tests reach the runtime's internals, which the library
# does not export. Windows switches task stacks with fibers,
# so has no assembly.
set(SPPC_SOURCES sources/sppc/sppc.c)
if (NOT WIN32)
  enable_language(ASM)
  list(APPEND SPPC_SOURCES headers/sppc/asm/gt_switch.S)
endif ()
add_library(
  sppc_objects OBJECT ${SPPC_SOURCES})

# msvc reads a source file without a byte-order mark in the system's
# code page, and makes narrow string literals in it too; sppc's text is
# UTF-8, as gcc and clang take it.
if (MSVC)
  target_compile_options(sppc_objects PUBLIC /utf-8)
endif ()

# The Windows threads sleep on "WaitOnAddress", which is in
# the synchronization api set rather than kernel32; sockets and
# their errors are Winsock's; random bytes are bcrypt's.
if (WIN32)
  target_link_libraries(sppc_objects PUBLIC synchronization ws2_32 bcrypt)
endif ()
set_target_properties(
  sppc_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(
  sppc_objects PUBLIC headers)
target_compile_definitions(
  sppc_objects PUBLIC
  _FILE_OFFSET_BITS=64
  DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)

# Unwind tables for every function, so the crash reporter's backtrace walks
# through sppc and, being public, the tests, and through a signal's frame.
# Already the default on x86, aarch64, s390x, powerpc and riscv; not on arm,
# mips, sparc or m68k.
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    sppc_objects PUBLIC $<$<COMPILE_LANGUAGE:C>:-fasynchronous-unwind-tables>)
endif ()

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
if (NOT WIN32 AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    sppc_objects PRIVATE
    "$<$<AND:$<COMPILE_LANGUAGE:C>,$<NOT:$<CONFIG:Debug>>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=$<IF:$<PLATFORM_ID:Darwin>,2,3>>")
endif ()

add_library(
  ${PROJECT_NAME} SHARED $<TARGET_OBJECTS:sppc_objects>)
target_link_libraries(
  ${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ${SPPC_UNWIND_LIBS}
  $<$<PLATFORM_ID:Windows>:synchronization ws2_32 bcrypt>)

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
    "LINKER:-z,text")
endif ()

if (SPPC_BUILD_TESTS)
  enable_testing()

  # Every test also runs against the blocking i/o backend, the
  # one a platform without its own starts on, where the ring
  # (Linux) or the completion port (Windows) would otherwise
  # hide it. Only where there is one to compare with: elsewhere
  # it is the only backend.
  set(SPPC_TEST_BACKENDS default)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
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
    target_compile_options(
      sppc_objects_blocking PUBLIC
      $<TARGET_PROPERTY:sppc_objects,INTERFACE_COMPILE_OPTIONS>)
    list(APPEND SPPC_TEST_BACKENDS blocking)

    # The ring's own fallback, taken when "io_uring_setup" fails
    # (an old kernel, or a sandbox): the default build run with
    # the call failed by seccomp. Not under an emulator: qemu-user
    # has no ring and no seccomp, so every run there is already
    # one without the ring.
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
      add_executable(no_uring tests/no_uring.c)
      target_include_directories(no_uring PRIVATE headers)
    endif ()
  endif ()

  # A test whose name ends in "_win" is of Windows' own parts, and
  # runs there only. Every other test is the public api's, for every
  # platform; Windows runs those its port has reached, listed here
  # until the list is all of them.
  file(GLOB SPPC_TESTS CONFIGURE_DEPENDS tests/test_*.c)
  if (WIN32)
    set(SPPC_WINDOWS_PORTED
      test_heap test_time test_threads test_fs test_mem test_net test_process
      test_gt_scheduling test_gt_stress test_gt_locks test_gt_safestack)
    list(TRANSFORM SPPC_WINDOWS_PORTED PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/tests/")
    list(TRANSFORM SPPC_WINDOWS_PORTED APPEND ".c")
    list(FILTER SPPC_TESTS INCLUDE REGEX "_win\\.c$")
    list(APPEND SPPC_TESTS ${SPPC_WINDOWS_PORTED})
  else ()
    list(FILTER SPPC_TESTS EXCLUDE REGEX "_win\\.c$")
  endif ()
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
        ${name} PRIVATE ${objects} Threads::Threads ${CMAKE_DL_LIBS} ${SPPC_UNWIND_LIBS})

      # The tests check with "assert", including calls made only
      # inside one, so a release build's NDEBUG must not compile
      # them away.
      target_compile_options(
        ${name} PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

      # Some tests count with C11 "_Atomic", which msvc's C compiler
      # takes only behind this switch (VS 2022 17.5 and later). The
      # library itself uses none.
      if (MSVC)
        target_compile_options(${name} PRIVATE /experimental:c11atomics)
        target_compile_options(${name} PRIVATE /Z7)
        target_compile_options(${name} PRIVATE /Ob0)
        target_link_options(${name} PRIVATE /DEBUG)
      endif ()
      add_test(
        NAME ${name} COMMAND ${name})

      # Heap damage from the runtime tends to surface far from its
      # cause. 77 is a test reporting it cannot run here (no ring
      # for example), shown as skipped.
      set(runs ${name})
      if (backend STREQUAL "default" AND TARGET no_uring)
        add_test(
          NAME ${name}_noring COMMAND no_uring $<TARGET_FILE:${name}>)
        list(APPEND runs ${name}_noring)
      endif ()
      set_tests_properties(${runs} PROPERTIES
        ENVIRONMENT "MALLOC_CHECK_=3" TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach ()
  endforeach ()

  # The headers stay standard C17, warning-free, with no extension
  # creeping back in. gcc and clang only: msvc has no equivalent
  # of -pedantic-errors.
  if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(strict_includes -I${CMAKE_CURRENT_SOURCE_DIR}/headers)
    if (SPPC_LIBUNWIND_INCLUDE)
      list(APPEND strict_includes -isystem ${SPPC_LIBUNWIND_INCLUDE})
    endif ()
    add_test(NAME test_strict_c17
      COMMAND ${CMAKE_C_COMPILER} -std=c17 -pedantic-errors -Wall -Wextra -Wvla -Werror
      -fsyntax-only -D_FILE_OFFSET_BITS=64 ${strict_includes}
      ${CMAKE_CURRENT_SOURCE_DIR}/sources/sppc/sppc.c)

    # It runs the compiler, not a built test, so a test run away from
    # the build host (x32's VM) leaves it behind: "ctest -LE host".
    set_tests_properties(test_strict_c17 PROPERTIES LABELS host)
  endif ()
endif ()
