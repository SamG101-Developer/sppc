#pragma once

/// Crash reporting. A handler on the fatal signals prints which
/// signal it was and a backtrace, then lets the signal kill the
/// process exactly as it would have without one, so the exit
/// status and any core dump are unchanged.

#include <sppc/config.h>

#if SPPC_OBJECT_ELF
  #include <sppc/backtrace/elf.h>
  #include <sppc/backtrace/posix.h>
#elif SPPC_KERNEL_DARWIN
  #include <sppc/backtrace/macho.h>
  #include <sppc/backtrace/posix.h>
#elif SPPC_PLATFORM_WINDOWS
  #include <sppc/backtrace/pe.h>
#endif
