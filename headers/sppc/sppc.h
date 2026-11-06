/// The public api, and the only header to include: everything is defined in
/// the files included here, chosen for the platform at compile time. The
/// platform's file includes the async runtime and the crash reporter itself,
/// once the pthreads and clocks they stand on are in place, so neither is
/// meant to be included on its own.

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
#include <sppc/types.h>
#include <sppc/errors.h>
#include <sppc/closure.h>

// The platform's file, which includes the async runtime and the crash
// reporter once what they stand on is in place, and the api over them.
#if SPPC_KERNEL_LINUX
  #include <sppc/platform/linux.h>
#elif SPPC_KERNEL_DARWIN
  #include <sppc/platform/darwin.h>
#elif SPPC_KERNEL_FREEBSD
  #include <sppc/platform/freebsd.h>
#elif SPPC_PLATFORM_WINDOWS
  #include <sppc/platform/windows.h>
#endif

#if SPPC_CC_GCC || SPPC_CC_CLANG
#pragma GCC diagnostic pop
#endif
