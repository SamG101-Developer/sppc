// The context switch for the s390x ELF calling convention (Linux on IBM Z).
// Saves r6-r14, f8-f15 and the fpc on the current stack, stores the stack
// pointer (r15) through the first argument (r2), loads the next one from the
// second (r3), and restores the same block from there. r14 holds the return
// address; the access registers hold the thread pointer, which a switch
// within one thread leaves alone.

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  aghi  %r15, -144
  stmg  %r6, %r14, 0(%r15)
  std   %f8,  72(%r15)
  std   %f9,  80(%r15)
  std   %f10, 88(%r15)
  std   %f11, 96(%r15)
  std   %f12, 104(%r15)
  std   %f13, 112(%r15)
  std   %f14, 120(%r15)
  std   %f15, 128(%r15)
  stfpc 136(%r15)

  stg   %r15, 0(%r2)
  lg    %r15, 0(%r3)

  lfpc  136(%r15)
  ld    %f15, 128(%r15)
  ld    %f14, 120(%r15)
  ld    %f13, 112(%r15)
  ld    %f12, 104(%r15)
  ld    %f11, 96(%r15)
  ld    %f10, 88(%r15)
  ld    %f9,  80(%r15)
  ld    %f8,  72(%r15)
  lmg   %r6, %r14, 0(%r15)
  aghi  %r15, 144
  br    %r14

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
