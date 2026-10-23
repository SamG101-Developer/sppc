// The context switch for the System V i386 calling convention (Linux).
// Saves the callee-saved registers and the x87 control word (and the sse
// one, when built with sse) on the current stack, stores the stack pointer
// through the first argument, loads the next one from the second, and
// restores the same block from there. Both arguments are on the stack.

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  mov  4(%esp), %eax
  mov  8(%esp), %edx

  push %ebp
  push %ebx
  push %esi
  push %edi

  sub  $8, %esp
#if defined(__SSE__)
  stmxcsr 0(%esp)
#endif
  fnstcw  4(%esp)

  mov  %esp, (%eax)
  mov  (%edx), %esp

#if defined(__SSE__)
  ldmxcsr 0(%esp)
#endif
  fldcw   4(%esp)
  add  $8, %esp

  pop  %edi
  pop  %esi
  pop  %ebx
  pop  %ebp
  ret

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
