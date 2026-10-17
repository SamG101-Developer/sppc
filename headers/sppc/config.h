#pragma once

#if defined (__APPLE__) && defined (__MACH__)
  #include <TargetConditionals.h>
#endif

// Platform selection macro suite, which detects the current
// operating system for os-specific code (file system, streams,
// networking, etc).
#define SPPC_PLATFORM_WINDOWS 0
#define SPPC_PLATFORM_UNIX    0
#define SPPC_PLATFORM_MACOS   0
#define SPPC_PLATFORM_ANDROID 0
#define SPPC_PLATFORM_IOS     0

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
  #undef SPPC_PLATFORM_WINDOWS
  #define SPPC_PLATFORM_WINDOWS 1
#elif defined (__ANDROID__)
  #undef SPPC_PLATFORM_ANDROID
  #define SPPC_PLATFORM_ANDROID 1
#elif defined (__linux__)
  #undef SPPC_PLATFORM_UNIX
  #define SPPC_PLATFORM_UNIX 1
#elif defined (__APPLE__) && defined (__MACH__) && \
  ((defined (TARGET_OS_OSX) && TARGET_OS_OSX) || (defined (TARGET_OS_MACCATALYST) && TARGET_OS_MACCATALYST))
  #undef SPPC_PLATFORM_MACOS
  #define SPPC_PLATFORM_MACOS 1
#elif defined (__APPLE__) && defined (__MACH__) && defined (TARGET_OS_IOS) && TARGET_OS_IOS
  #undef SPPC_PLATFORM_IOS
  #define SPPC_PLATFORM_IOS 1
#else
  #error "SPPC: Unsupported platform"
#endif

// Platforms that share a kernel share everything that is the
// kernel's: io_uring, the Linux syscalls and the ELF loader
// for Linux and Android; kqueue, Mach-O and the Darwin libc
// for macOS and iOS. Code keyed on those uses these.
#define SPPC_KERNEL_LINUX  (SPPC_PLATFORM_UNIX || SPPC_PLATFORM_ANDROID)
#define SPPC_KERNEL_DARWIN (SPPC_PLATFORM_MACOS || SPPC_PLATFORM_IOS)

// Compiler selection macro suite, which detects the current
// compiler for attribute-specifics (gnu::/clang:: attributes
// like hot/cold and other optimizations).
#define SPPC_CC_GCC   0
#define SPPC_CC_CLANG 0
#define SPPC_CC_MSVC  0

#if defined(__clang__)
  #undef  SPPC_CC_CLANG
  #define SPPC_CC_CLANG 1
#elif defined(__GNUC__) && !defined(__clang__)
  #undef  SPPC_CC_GCC
  #define SPPC_CC_GCC 1
#elif defined(_MSC_VER)
  #undef  SPPC_CC_MSVC
  #define SPPC_CC_MSVC 1
#else
  #error "SPPC: Unsupported compiler"
#endif


// Architecture selection macro suite, which detects the current
// architecture particularly for the async runtime environment,
// which uses specialized assembly code.
#define SPPC_ARCH_X86_64 0
#define SPPC_ARCH_X86    0
#define SPPC_ARCH_ARM64  0
#define SPPC_ARCH_ARM    0

#if defined(__x86_64__) || defined(_M_X64)
  #undef  SPPC_ARCH_X86_64
  #define SPPC_ARCH_X86_64 1
#elif defined(__i386__) || defined(_M_IX86)
  #undef  SPPC_ARCH_X86
  #define SPPC_ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
  #undef  SPPC_ARCH_ARM64
  #define SPPC_ARCH_ARM64 1
#elif defined(__arm__) || defined(_M_ARM)
  #undef  SPPC_ARCH_ARM
  #define SPPC_ARCH_ARM 1
#else
  #error "SPPC: Unsupported architecture"
#endif

// ABI selection macro suite, which detects the current ABI
// for the async runtime environment, which uses specialized
// assembly code.
#define SPPC_ABI_SYSV      0
#define SPPC_ABI_WIN64     0
#define SPPC_ABI_AAPCS64   0
#define SPPC_ABI_WIN_ARM64 0

#if SPPC_PLATFORM_WINDOWS && SPPC_ARCH_X86_64
  #undef  SPPC_ABI_WIN64
  #define SPPC_ABI_WIN64 1
#elif SPPC_PLATFORM_WINDOWS && SPPC_ARCH_ARM64
  #undef  SPPC_ABI_WIN_ARM64
  #define SPPC_ABI_WIN_ARM64 1
#elif SPPC_ARCH_X86_64
  #undef  SPPC_ABI_SYSV
  #define SPPC_ABI_SYSV 1
#elif SPPC_ARCH_ARM64
  #undef  SPPC_ABI_AAPCS64
  #define SPPC_ABI_AAPCS64 1
#else
  #error "SPPC: Unsupported ABI"
#endif

// Set from cmake; a build that doesn't say is a release build.
#ifndef DEBUG_BUILD
  #define DEBUG_BUILD 0
#endif
