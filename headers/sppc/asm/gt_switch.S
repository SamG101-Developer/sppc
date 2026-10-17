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
#else
  #error "the green-thread runtime has no context switch for this calling convention"
#endif
