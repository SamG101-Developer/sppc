/// The public api. Everything is defined in the files
/// included here, chosen for the platform at compile
/// time: the async runtime, crash reporting, and the
/// platform layer that implements the "sppc_*" functions
/// themselves.

#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/config.h>

#if SPPC_CC_GCC || SPPC_CC_CLANG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

#include <sppc/macros.h>
#include <sppc/closure.h>
#include <sppc/async.h>
#include <sppc/backtrace.h>

#if SPPC_KERNEL_LINUX
  #include <sppc/platform/linux.h>
#elif SPPC_KERNEL_DARWIN
  #include <sppc/platform/darwin.h>
#elif SPPC_PLATFORM_WINDOWS
  #include <sppc/platform/windows.h>
#endif

#if SPPC_CC_GCC || SPPC_CC_CLANG
#pragma GCC diagnostic pop
#endif
