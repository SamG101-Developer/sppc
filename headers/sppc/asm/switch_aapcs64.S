// The context switch for the AAPCS64 calling convention (Linux arm64).
// Saves x19-x30, d8-d15 and fpcr on the current stack, stores the stack
// pointer through the first argument, loads the next one from the second,
// and restores the same block from there.

.text
  .p2align 4
  .globl _gt_switch
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _gt_switch
  .type  _gt_switch, @function

_gt_switch:
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

  .size _gt_switch, . - _gt_switch

.section .note.GNU-stack,"",@progbits
