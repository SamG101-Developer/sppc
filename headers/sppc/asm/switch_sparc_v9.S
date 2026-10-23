// The context switch for the SPARC V9 64-bit calling convention (Linux
// sparc64). A function's locals and ins live in its register window, which
// the hardware spills to the 16-word save area at its stack pointer (biased
// by -2047), so a task's state is its windows, all in memory once flushed:
//
// - "save" gives the switch a window of its own, which makes the caller's
//   an older one, and "flushw" spills every older window to its frame;
// - the switch's own window, the one "flushw" leaves, is stored by hand in
//   its own save area, with fsr (no fp register is callee-saved here);
// - then the stack pointer is stored through the first argument, the next
//   one loaded from the second, and that task's window loaded from its save
//   area. "ret" returns to its i7 + 8, and the "restore" in its delay slot
//   finds no window to restore, so the trap fills it from memory: the next
//   task's caller, as it was flushed.
//
// g7 holds the thread pointer, which a switch within one thread leaves alone.

.text
  .p2align 2
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, #function

_SPPC_GT_SWITCH:
  save   %sp, -176, %sp
  flushw
  stx    %l0, [%sp + 2047 + 0]
  stx    %l1, [%sp + 2047 + 8]
  stx    %l2, [%sp + 2047 + 16]
  stx    %l3, [%sp + 2047 + 24]
  stx    %l4, [%sp + 2047 + 32]
  stx    %l5, [%sp + 2047 + 40]
  stx    %l6, [%sp + 2047 + 48]
  stx    %l7, [%sp + 2047 + 56]
  stx    %i0, [%sp + 2047 + 64]
  stx    %i1, [%sp + 2047 + 72]
  stx    %i2, [%sp + 2047 + 80]
  stx    %i3, [%sp + 2047 + 88]
  stx    %i4, [%sp + 2047 + 96]
  stx    %i5, [%sp + 2047 + 104]
  stx    %i6, [%sp + 2047 + 112]
  stx    %i7, [%sp + 2047 + 120]
  stx    %fsr, [%sp + 2047 + 128]

  stx    %sp, [%i0]
  ldx    [%i1], %sp

  ldx    [%sp + 2047 + 128], %fsr
  ldx    [%sp + 2047 + 0], %l0
  ldx    [%sp + 2047 + 8], %l1
  ldx    [%sp + 2047 + 16], %l2
  ldx    [%sp + 2047 + 24], %l3
  ldx    [%sp + 2047 + 32], %l4
  ldx    [%sp + 2047 + 40], %l5
  ldx    [%sp + 2047 + 48], %l6
  ldx    [%sp + 2047 + 56], %l7
  ldx    [%sp + 2047 + 64], %i0
  ldx    [%sp + 2047 + 72], %i1
  ldx    [%sp + 2047 + 80], %i2
  ldx    [%sp + 2047 + 88], %i3
  ldx    [%sp + 2047 + 96], %i4
  ldx    [%sp + 2047 + 104], %i5
  ldx    [%sp + 2047 + 112], %i6
  ldx    [%sp + 2047 + 120], %i7
  ret
  restore

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
