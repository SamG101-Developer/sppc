# Find every dependency.

find_package(Threads REQUIRED)

# Without glibc (musl), the crash reporter walks the stack with libunwind:
# musl has no "backtrace", and its libgcc cannot step through a signal frame.
# A static libunwind (CI builds one, position-independent) goes into the
# library whole, its symbols kept inside it, so the library needs only libc.
include(CheckSymbolExists)
check_symbol_exists(__GLIBC__ "features.h" SPPC_GLIBC)
set(SPPC_UNWIND_LIBS)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT SPPC_GLIBC)
  find_library(SPPC_LIBUNWIND unwind REQUIRED)
  find_path(SPPC_LIBUNWIND_INCLUDE libunwind.h REQUIRED)
  include_directories(SYSTEM ${SPPC_LIBUNWIND_INCLUDE})
  set(SPPC_UNWIND_LIBS ${SPPC_LIBUNWIND})
  if (SPPC_LIBUNWIND MATCHES "\\.a$")
    list(APPEND SPPC_UNWIND_LIBS "-Wl,--exclude-libs,libunwind.a")
  endif ()
endif ()
