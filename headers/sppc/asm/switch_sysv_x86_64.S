// The context switch for the System V x86_64 calling convention (Linux, macOS,
// and Linux x32).
// Saves the callee-saved registers and the sse/x87 control words on the
// current stack, stores the stack pointer through the first argument, loads
// the next one from the second, and restores the same block from there.

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
  push %rbp
  push %rbx
  push %r12
  push %r13
  push %r14
  push %r15

  sub  $8, %rsp
  stmxcsr 0(%rsp)
  fnstcw  4(%rsp)

#if defined(__ILP32__)
  // x32: the same convention with 4-byte pointers, so the slots are 4
  // bytes; the stack is below 4GiB, so its pointer's top half is 0.
  mov  %esp, (%rdi)
  mov  (%rsi), %esp
#else
  mov  %rsp, (%rdi)
  mov  (%rsi), %rsp
#endif

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

#if !SPPC_KERNEL_DARWIN
  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted. Mach-O never gives one, so has no such note.
.section .note.GNU-stack,"",@progbits
#endif
