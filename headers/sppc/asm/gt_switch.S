// The context switch, picked by calling convention: the registers a switch
// has to save are the ones the convention makes callee-saved, so it is the
// convention that decides the file, not the cpu. Built as one object so the
// build and the tests name the same file on every platform.

#include <sppc/config.h>

// The symbol's name in the object file. Mach-O prefixes every C name with
// an underscore, so the "_gt_switch" C calls is "__gt_switch" there.
#if SPPC_KERNEL_DARWIN
  #define _SPPC_GT_SWITCH __gt_switch
#else
  #define _SPPC_GT_SWITCH _gt_switch
#endif

#if SPPC_ABI_SYSV
  #include "switch_sysv_x86_64.S"
#elif SPPC_ABI_AAPCS64
  #include "switch_aapcs64.S"
#elif SPPC_ABI_S390X
  #include "switch_s390x.S"
#elif SPPC_ABI_SYSV_I386
  #include "switch_sysv_i386.S"
#elif SPPC_ABI_PPC64_ELFV2 || SPPC_ABI_PPC64_ELFV1
  #include "switch_ppc64.S"
#elif SPPC_ABI_PPC_SYSV
  #include "switch_ppc_sysv.S"
#elif SPPC_ABI_RISCV_LP64D
  #include "switch_riscv_lp64d.S"
#elif SPPC_ABI_AAPCS32
  #include "switch_aapcs32.S"
#elif SPPC_ABI_MIPS_O32
  #include "switch_mips_o32.S"
#elif SPPC_ABI_MIPS_N64
  #include "switch_mips_n64.S"
#elif SPPC_ABI_LOONGARCH_LP64D
  #include "switch_loongarch_lp64d.S"
#elif SPPC_ABI_M68K_SYSV
  #include "switch_m68k_sysv.S"
#elif SPPC_ABI_SPARC_V9
  #include "switch_sparc_v9.S"
#else
  #error "the green-thread runtime has no context switch for this calling convention"
#endif
