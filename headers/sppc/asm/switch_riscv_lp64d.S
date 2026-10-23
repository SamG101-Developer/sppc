// The context switch for the RISC-V LP64D calling convention (Linux riscv64).
// Saves ra, s0-s11, fs0-fs11 and fcsr in a 208-byte frame on the current
// stack, stores the stack pointer through the first argument (a0), loads the
// next one from the second (a1), and restores the same block from there. gp
// and tp hold the global and thread pointers, which a switch within one
// thread leaves alone.

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  addi  sp, sp, -208
  sd    ra, 0(sp)
  sd    s0, 8(sp)
  sd    s1, 16(sp)
  sd    s2, 24(sp)
  sd    s3, 32(sp)
  sd    s4, 40(sp)
  sd    s5, 48(sp)
  sd    s6, 56(sp)
  sd    s7, 64(sp)
  sd    s8, 72(sp)
  sd    s9, 80(sp)
  sd    s10, 88(sp)
  sd    s11, 96(sp)
  fsd   fs0, 104(sp)
  fsd   fs1, 112(sp)
  fsd   fs2, 120(sp)
  fsd   fs3, 128(sp)
  fsd   fs4, 136(sp)
  fsd   fs5, 144(sp)
  fsd   fs6, 152(sp)
  fsd   fs7, 160(sp)
  fsd   fs8, 168(sp)
  fsd   fs9, 176(sp)
  fsd   fs10, 184(sp)
  fsd   fs11, 192(sp)
  frcsr t0
  sd    t0, 200(sp)

  sd    sp, 0(a0)
  ld    sp, 0(a1)

  ld    t0, 200(sp)
  fscsr t0
  fld   fs11, 192(sp)
  fld   fs10, 184(sp)
  fld   fs9, 176(sp)
  fld   fs8, 168(sp)
  fld   fs7, 160(sp)
  fld   fs6, 152(sp)
  fld   fs5, 144(sp)
  fld   fs4, 136(sp)
  fld   fs3, 128(sp)
  fld   fs2, 120(sp)
  fld   fs1, 112(sp)
  fld   fs0, 104(sp)
  ld    s11, 96(sp)
  ld    s10, 88(sp)
  ld    s9, 80(sp)
  ld    s8, 72(sp)
  ld    s7, 64(sp)
  ld    s6, 56(sp)
  ld    s5, 48(sp)
  ld    s4, 40(sp)
  ld    s3, 32(sp)
  ld    s2, 24(sp)
  ld    s1, 16(sp)
  ld    s0, 8(sp)
  ld    ra, 0(sp)
  addi  sp, sp, 208
  ret

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
