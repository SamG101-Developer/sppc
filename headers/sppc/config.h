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
#define SPPC_PLATFORM_FREEBSD 0

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
#elif defined (__FreeBSD__)
  #undef SPPC_PLATFORM_FREEBSD
  #define SPPC_PLATFORM_FREEBSD 1
#else
  #error "SPPC: Unsupported platform"
#endif

// Platforms that share a kernel share everything that is the
// kernel's: io_uring, the Linux syscalls and the ELF loader
// for Linux and Android; kqueue, Mach-O and the Darwin libc
// for macOS and iOS. Code keyed on those uses these.
#define SPPC_KERNEL_LINUX   (SPPC_PLATFORM_UNIX || SPPC_PLATFORM_ANDROID)
#define SPPC_KERNEL_DARWIN  (SPPC_PLATFORM_MACOS || SPPC_PLATFORM_IOS)
#define SPPC_KERNEL_FREEBSD SPPC_PLATFORM_FREEBSD

// Kernels whose programs and libraries are ELF objects, which
// the crash reporter reads the symbols of.
#define SPPC_OBJECT_ELF (SPPC_KERNEL_LINUX || SPPC_KERNEL_FREEBSD)

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
#define SPPC_ARCH_S390X  0
#define SPPC_ARCH_PPC64  0
#define SPPC_ARCH_PPC    0
#define SPPC_ARCH_RISCV64 0
#define SPPC_ARCH_MIPS   0
#define SPPC_ARCH_MIPS64 0
#define SPPC_ARCH_LOONGARCH64 0
#define SPPC_ARCH_M68K   0
#define SPPC_ARCH_SPARC64 0

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
#elif defined(__s390x__)
  #undef  SPPC_ARCH_S390X
  #define SPPC_ARCH_S390X 1
#elif defined(__powerpc64__)
  #undef  SPPC_ARCH_PPC64
  #define SPPC_ARCH_PPC64 1
#elif defined(__powerpc__)
  #undef  SPPC_ARCH_PPC
  #define SPPC_ARCH_PPC 1
#elif defined(__riscv) && __riscv_xlen == 64
  #undef  SPPC_ARCH_RISCV64
  #define SPPC_ARCH_RISCV64 1
#elif defined(__mips__) && _MIPS_SIM == _ABIO32
  #undef  SPPC_ARCH_MIPS
  #define SPPC_ARCH_MIPS 1
#elif defined(__mips__) && _MIPS_SIM == _ABI64
  #undef  SPPC_ARCH_MIPS64
  #define SPPC_ARCH_MIPS64 1
#elif defined(__loongarch__) && __loongarch_grlen == 64
  #undef  SPPC_ARCH_LOONGARCH64
  #define SPPC_ARCH_LOONGARCH64 1
#elif defined(__m68k__)
  #undef  SPPC_ARCH_M68K
  #define SPPC_ARCH_M68K 1
#elif defined(__sparc__) && defined(__arch64__)
  #undef  SPPC_ARCH_SPARC64
  #define SPPC_ARCH_SPARC64 1
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
#define SPPC_ABI_S390X     0
#define SPPC_ABI_SYSV_I386 0
#define SPPC_ABI_PPC64_ELFV2 0
#define SPPC_ABI_PPC64_ELFV1 0
#define SPPC_ABI_PPC_SYSV  0
#define SPPC_ABI_RISCV_LP64D 0
#define SPPC_ABI_AAPCS32   0
#define SPPC_ABI_MIPS_O32  0
#define SPPC_ABI_MIPS_N64  0
#define SPPC_ABI_LOONGARCH_LP64D 0
#define SPPC_ABI_M68K_SYSV 0
#define SPPC_ABI_SPARC_V9  0

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
#elif SPPC_ARCH_S390X
  #undef  SPPC_ABI_S390X
  #define SPPC_ABI_S390X 1
#elif SPPC_ARCH_X86 && !SPPC_PLATFORM_WINDOWS
  #undef  SPPC_ABI_SYSV_I386
  #define SPPC_ABI_SYSV_I386 1
#elif SPPC_ARCH_PPC64 && defined(_CALL_ELF) && _CALL_ELF == 2
  #undef  SPPC_ABI_PPC64_ELFV2
  #define SPPC_ABI_PPC64_ELFV2 1
#elif SPPC_ARCH_PPC64
  #undef  SPPC_ABI_PPC64_ELFV1
  #define SPPC_ABI_PPC64_ELFV1 1
#elif SPPC_ARCH_PPC
  #undef  SPPC_ABI_PPC_SYSV
  #define SPPC_ABI_PPC_SYSV 1
#elif SPPC_ARCH_RISCV64 && defined(__riscv_float_abi_double)
  #undef  SPPC_ABI_RISCV_LP64D
  #define SPPC_ABI_RISCV_LP64D 1
#elif SPPC_ARCH_ARM && defined(__ARM_EABI__) && !SPPC_PLATFORM_WINDOWS
  #undef  SPPC_ABI_AAPCS32
  #define SPPC_ABI_AAPCS32 1
#elif SPPC_ARCH_MIPS
  #undef  SPPC_ABI_MIPS_O32
  #define SPPC_ABI_MIPS_O32 1
#elif SPPC_ARCH_MIPS64
  #undef  SPPC_ABI_MIPS_N64
  #define SPPC_ABI_MIPS_N64 1
#elif SPPC_ARCH_LOONGARCH64 && defined(__loongarch_double_float)
  #undef  SPPC_ABI_LOONGARCH_LP64D
  #define SPPC_ABI_LOONGARCH_LP64D 1
#elif SPPC_ARCH_M68K
  #undef  SPPC_ABI_M68K_SYSV
  #define SPPC_ABI_M68K_SYSV 1
#elif SPPC_ARCH_SPARC64
  #undef  SPPC_ABI_SPARC_V9
  #define SPPC_ABI_SPARC_V9 1
#else
  #error "SPPC: Unsupported ABI"
#endif

// Byte order, for the layouts that differ with it. msvc only targets
// little-endian machines.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  #define SPPC_ENDIAN_BIG 1
#else
  #define SPPC_ENDIAN_BIG 0
#endif

// Set from cmake; a build that doesn't say is a release build.
#ifndef DEBUG_BUILD
  #define DEBUG_BUILD 0
#endif
