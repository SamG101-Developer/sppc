#pragma once

/// Crash reporting. A handler on the fatal signals prints which signal it was
/// and a backtrace, then lets the signal kill the process exactly as it would
/// have without one, so the exit status and any core dump are unchanged.
///
/// The walk is glibc's "backtrace", which follows ".eh_frame"; the compiler
/// gives every s++ function an entry there for this. The names come out of
/// each object's own ".symtab" rather than "dladdr", because every s++
/// function has internal linkage and "dladdr" only sees exported symbols.
///
/// A handler may only call what is async-signal-safe. Everything here is,
/// apart from "backtrace" (whose one unsafe step, loading libgcc_s, is done
/// ahead of time at install) and "dladdr" (which reads the link map and does
/// not allocate).
///
/// The reporter is picked per platform: backtrace/elf.h on Linux, with
/// backtrace/macho.h (Darwin) and backtrace/pe.h (Windows) to come. The
/// buffer and formatting they share are in backtrace/common.h.

#include <sppc/config.h>

#if SPPC_KERNEL_LINUX
  #include <sppc/backtrace/elf.h>
#elif SPPC_KERNEL_DARWIN
  #include <sppc/backtrace/macho.h>
#elif SPPC_PLATFORM_WINDOWS
  #include <sppc/backtrace/pe.h>
#endif
