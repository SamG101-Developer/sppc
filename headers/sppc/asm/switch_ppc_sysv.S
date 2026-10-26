// The context switch for the 32-bit PowerPC System V calling convention
// (Linux powerpc). Saves r14-r31, f14-f31 (where there is an fpu), v20-v31
// (where there is altivec), lr, cr and the fpscr in a 432-byte frame on the
// current stack, stores the stack pointer (r1) through the first argument
// (r3), loads the next one from the second (r4), and restores the same block
// from there. r2 is the thread pointer, not the caller's to keep, so it is
// left alone. Registers are numbered, not named: the assembler only takes
// names with -mregnames.
//
//   0       back chain         224-415  v20-v31
//   8-79    r14-r31            416      lr
//   80-223  f14-f31            420      cr     424  fpscr

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  mflr  0
  mfcr  12
  stwu  1, -432(1)
  stw   0, 416(1)
  stw   12, 420(1)
  stmw  14, 8(1)
#if !defined(_SOFT_FLOAT)
  stfd  14, 80(1)
  stfd  15, 88(1)
  stfd  16, 96(1)
  stfd  17, 104(1)
  stfd  18, 112(1)
  stfd  19, 120(1)
  stfd  20, 128(1)
  stfd  21, 136(1)
  stfd  22, 144(1)
  stfd  23, 152(1)
  stfd  24, 160(1)
  stfd  25, 168(1)
  stfd  26, 176(1)
  stfd  27, 184(1)
  stfd  28, 192(1)
  stfd  29, 200(1)
  stfd  30, 208(1)
  stfd  31, 216(1)
#endif
#if defined(__ALTIVEC__)
  li    11, 224
  stvx  20, 1, 11
  li    11, 240
  stvx  21, 1, 11
  li    11, 256
  stvx  22, 1, 11
  li    11, 272
  stvx  23, 1, 11
  li    11, 288
  stvx  24, 1, 11
  li    11, 304
  stvx  25, 1, 11
  li    11, 320
  stvx  26, 1, 11
  li    11, 336
  stvx  27, 1, 11
  li    11, 352
  stvx  28, 1, 11
  li    11, 368
  stvx  29, 1, 11
  li    11, 384
  stvx  30, 1, 11
  li    11, 400
  stvx  31, 1, 11
#endif
#if !defined(_SOFT_FLOAT)
  mffs  0
  stfd  0, 424(1)
#endif

  stw   1, 0(3)
  lwz   1, 0(4)

#if !defined(_SOFT_FLOAT)
  lfd   0, 424(1)
  mtfsf 0xff, 0
#endif
#if defined(__ALTIVEC__)
  li    11, 400
  lvx   31, 1, 11
  li    11, 384
  lvx   30, 1, 11
  li    11, 368
  lvx   29, 1, 11
  li    11, 352
  lvx   28, 1, 11
  li    11, 336
  lvx   27, 1, 11
  li    11, 320
  lvx   26, 1, 11
  li    11, 304
  lvx   25, 1, 11
  li    11, 288
  lvx   24, 1, 11
  li    11, 272
  lvx   23, 1, 11
  li    11, 256
  lvx   22, 1, 11
  li    11, 240
  lvx   21, 1, 11
  li    11, 224
  lvx   20, 1, 11
#endif
#if !defined(_SOFT_FLOAT)
  lfd   31, 216(1)
  lfd   30, 208(1)
  lfd   29, 200(1)
  lfd   28, 192(1)
  lfd   27, 184(1)
  lfd   26, 176(1)
  lfd   25, 168(1)
  lfd   24, 160(1)
  lfd   23, 152(1)
  lfd   22, 144(1)
  lfd   21, 136(1)
  lfd   20, 128(1)
  lfd   19, 120(1)
  lfd   18, 112(1)
  lfd   17, 104(1)
  lfd   16, 96(1)
  lfd   15, 88(1)
  lfd   14, 80(1)
#endif
  lmw   14, 8(1)
  lwz   12, 420(1)
  mtcrf 0x38, 12
  lwz   0, 416(1)
  mtlr  0
  addi  1, 1, 432
  blr

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
