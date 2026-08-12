# Tests compile the library sources directly rather than linking libsppc.so,
# because they assert on internal state (gt_task_pool, _stdout_mutex) that the
# version script deliberately hides from the shared library. test_exports is
# the exception: it checks the real .so's public surface.

add_library(sppc_testable STATIC
  ${CMAKE_SOURCE_DIR}/sources/sppc/sppc.c
  ${CMAKE_SOURCE_DIR}/headers/sppc/asm/gt_switch.S)

target_include_directories(sppc_testable PUBLIC ${CMAKE_SOURCE_DIR}/headers)
target_compile_features(sppc_testable PUBLIC c_std_23)
target_compile_definitions(sppc_testable PUBLIC
  _FILE_OFFSET_BITS=64
  DEBUG_BUILD=$<IF:$<CONFIG:Debug>,1,0>)

function(sppc_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE sppc_testable)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
  # A regression in the stdio guard hangs rather than failing, so every test
  # gets a wall-clock bound.
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

sppc_add_test(test_pthread_errors test_pthread_errors.c)
sppc_add_test(test_stdio_guard    test_stdio_guard.c)
sppc_add_test(test_gt_stack       test_gt_stack.c)
sppc_add_test(test_sockaddr       test_sockaddr.c)
sppc_add_test(test_mmap           test_mmap.c)
sppc_add_test(test_async          test_async.c test_async_tu2.c)

add_test(NAME test_exports
  COMMAND ${CMAKE_COMMAND} -DLIB=$<TARGET_FILE:sppc>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test_exports.cmake)
set_tests_properties(test_exports PROPERTIES TIMEOUT 60)