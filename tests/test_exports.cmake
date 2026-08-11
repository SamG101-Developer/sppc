# Regression test: libsppc.so must export the sppc_* API and nothing else.
#
# The version script (sppc.map) keeps the green-thread runtime and the stdio
# mutexes internal. Run with -DLIB=<path to libsppc.so>.
#
# Note: `nm -gD` also lists undefined imports from libc, which are not exports.
# --defined-only is what answers "what does this library offer".

find_program(NM_EXE NAMES nm)
if (NOT NM_EXE)
  message(STATUS "nm not found; skipping export-surface test")
  return()
endif()

execute_process(
  COMMAND ${NM_EXE} -gD --defined-only ${LIB}
  OUTPUT_VARIABLE out
  RESULT_VARIABLE rc
  ERROR_VARIABLE err)

if (NOT rc EQUAL 0)
  message(FATAL_ERROR "nm failed on ${LIB}: ${err}")
endif()

string(REPLACE "\n" ";" lines "${out}")
set(exported "")
set(leaked "")
foreach (line IN LISTS lines)
  if (line STREQUAL "")
    continue()
  endif()
  # "<addr> <type> <name>"
  string(REGEX REPLACE "^[0-9a-fA-F]* +[A-Za-z] +" "" name "${line}")
  list(APPEND exported "${name}")
  if (NOT name MATCHES "^sppc_")
    list(APPEND leaked "${name}")
  endif()
endforeach()

list(LENGTH exported n_exported)
list(LENGTH leaked n_leaked)

if (n_exported EQUAL 0)
  message(FATAL_ERROR
    "libsppc.so exports nothing.\n"
    "Every sppc_* function is defined `inline` in the header, so the extern\n"
    "re-declarations in sources/sppc/sppc.c are what force the symbols to be\n"
    "emitted. Check they are still in sync with the header.")
endif()

if (n_leaked GREATER 0)
  string(REPLACE ";" ", " leaked_str "${leaked}")
  message(FATAL_ERROR "non-sppc_ symbols leaked from libsppc.so: ${leaked_str}")
endif()

message(STATUS "  ${n_exported} exported symbols, all sppc_*  PASS")