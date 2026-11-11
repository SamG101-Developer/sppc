// The context switch for the AAPCS64 calling convention (Linux and macOS arm64;
// Apple's variant keeps the same callee-saved registers, and x18, which it
// reserves, is never touched here).
// Saves x19-x30, d8-d15 and fpcr on the current stack, stores the stack
// pointer through the first argument, loads the next one from the second,
// and restores the same block from there.

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  // ELF also takes its type and size; Mach-O has neither.
#if SPPC_KERNEL_DARWIN
  .private_extern _SPPC_GT_SWITCH
#else
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function
#endif

_SPPC_GT_SWITCH:
#if defined(__ARM_FEATURE_BTI_DEFAULT)
  bti  c
#endif
  sub  sp, sp, #176
  stp  x19, x20, [sp, #0]
  stp  x21, x22, [sp, #16]
  stp  x23, x24, [sp, #32]
  stp  x25, x26, [sp, #48]
  stp  x27, x28, [sp, #64]
  stp  x29, x30, [sp, #80]
  stp  d8,  d9,  [sp, #96]
  stp  d10, d11, [sp, #112]
  stp  d12, d13, [sp, #128]
  stp  d14, d15, [sp, #144]
  mrs  x2, fpcr
  str  x2, [sp, #160]

  mov  x2, sp
  str  x2, [x0]
  ldr  x2, [x1]
  mov  sp, x2

  ldr  x2, [sp, #160]
  msr  fpcr, x2
  ldp  d14, d15, [sp, #144]
  ldp  d12, d13, [sp, #128]
  ldp  d10, d11, [sp, #112]
  ldp  d8,  d9,  [sp, #96]
  ldp  x29, x30, [sp, #80]
  ldp  x27, x28, [sp, #64]
  ldp  x25, x26, [sp, #48]
  ldp  x23, x24, [sp, #32]
  ldp  x21, x22, [sp, #16]
  ldp  x19, x20, [sp, #0]
  add  sp, sp, #176
  ret

#if !SPPC_KERNEL_DARWIN
  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted. Mach-O never gives one, so has no such note.
.section .note.GNU-stack,"",@progbits

// Built with landing pads or signed return addresses ("-mbranch-protection"):
// say so, or the linker drops the library's property, as it keeps one only
// when every object has it. A GNU property note: the AArch64 feature word,
// BTI its bit 0 and PAC its bit 1. The switch signs nothing itself: the
// return address it saves and restores is the caller's, as "bl" left it.
#if defined(__ARM_FEATURE_BTI_DEFAULT) || defined(__ARM_FEATURE_PAC_DEFAULT)
#if defined(__ARM_FEATURE_BTI_DEFAULT)
  #define _SPPC_GT_BTI 1
#else
  #define _SPPC_GT_BTI 0
#endif
#if defined(__ARM_FEATURE_PAC_DEFAULT)
  #define _SPPC_GT_PAC 2
#else
  #define _SPPC_GT_PAC 0
#endif
.pushsection .note.gnu.property, "a"
  .p2align 3
  .long 4           // the name's size, "GNU" and its nul
  .long 16          // the description's size: one property, padded to 8
  .long 5           // NT_GNU_PROPERTY_TYPE_0
  .asciz "GNU"
  .long 0xc0000000  // GNU_PROPERTY_AARCH64_FEATURE_1_AND
  .long 4           // its data's size
  .long _SPPC_GT_BTI | _SPPC_GT_PAC
  .long 0           // padding to 8
.popsection
#endif
#endif
