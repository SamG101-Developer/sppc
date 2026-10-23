// The context switch for the m68k System V calling convention (Linux m68k).
// Saves d2-d7 and a2-a6, and with a 68881 fp2-fp7 and fpcr too, on the
// current stack, stores the stack pointer through the first argument, loads
// the next one from the second, and restores the same block from there. Both
// arguments are on the stack; the return address is too, so "rts" resumes.

.text
  .p2align 1
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  move.l   4(%sp), %a0
  move.l   8(%sp), %a1

  movem.l  %d2-%d7/%a2-%a6, -(%sp)
#if defined(__HAVE_68881__)
  fmovem.x %fp2-%fp7, -(%sp)
  fmove.l  %fpcr, -(%sp)
#endif

  move.l   %sp, (%a0)
  move.l   (%a1), %sp

#if defined(__HAVE_68881__)
  fmove.l  (%sp)+, %fpcr
  fmovem.x (%sp)+, %fp2-%fp7
#endif
  movem.l  (%sp)+, %d2-%d7/%a2-%a6
  rts

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
