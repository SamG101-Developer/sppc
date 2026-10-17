// The context switch for the System V x86_64 calling convention (Linux).
// Saves the callee-saved registers and the sse/x87 control words on the
// current stack, stores the stack pointer through the first argument, loads
// the next one from the second, and restores the same block from there.

.text
  .p2align 4
  .globl _gt_switch
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _gt_switch
  .type  _gt_switch, @function

_gt_switch:
  push %rbp
  push %rbx
  push %r12
  push %r13
  push %r14
  push %r15

  sub  $8, %rsp
  stmxcsr 0(%rsp)
  fnstcw  4(%rsp)

  mov  %rsp, (%rdi)
  mov  (%rsi), %rsp

  ldmxcsr 0(%rsp)
  fldcw   4(%rsp)
  add  $8, %rsp

  pop  %r15
  pop  %r14
  pop  %r13
  pop  %r12
  pop  %rbx
  pop  %rbp
  ret

  .size _gt_switch, . - _gt_switch

.section .note.GNU-stack,"",@progbits
