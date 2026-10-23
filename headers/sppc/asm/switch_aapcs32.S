// The context switch for the 32-bit ARM AAPCS calling convention (Linux armv7
// hard float, and armv5te soft float). Saves r4-r11 and lr, and with a vfp
// d8-d15 and fpscr too, on the current stack, stores the stack pointer
// through the first argument (r0), loads the next one from the second (r1),
// and restores the same block from there. r3 rides along only to keep the
// stack 8-byte aligned. ARM code, returning with "bx", so either kind of
// caller and entry point works: a thumb one's address has its low bit set.

  .syntax unified
  .arm
.text
  .p2align 2
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, %function

_SPPC_GT_SWITCH:
  push  {r3-r11, lr}
#if defined(__ARM_FP)
  vpush {d8-d15}
  vmrs  r2, fpscr
  push  {r2, r3}
#endif

  str   sp, [r0]
  ldr   sp, [r1]

#if defined(__ARM_FP)
  pop   {r2, r3}
  vmsr  fpscr, r2
  vpop  {d8-d15}
#endif
  pop   {r3-r11, lr}
  bx    lr

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",%progbits
