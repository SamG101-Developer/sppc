// The context switch for the 64-bit PowerPC calling conventions on Linux:
// ELFv2 (ppc64le) and ELFv1 (big-endian ppc64). Saves r14-r31, f14-f31,
// v20-v31 (where there is altivec), lr, r2 (the toc), cr and the fpscr in a
// 544-byte frame on the current stack, stores the stack pointer (r1) through
// the first argument (r3), loads the next one from the second (r4), and
// restores the same block from there. Registers are numbered, not named: the
// assembler only takes names with -mregnames.
//
//   0       back chain         320-511  v20-v31
//   32-175  r14-r31            512      lr
//   176-319 f14-f31            520      r2
//                              528      cr     536  fpscr
//
// It returns by branching to the saved lr through r12, which is what ELFv2
// wants at a function's global entry point: a new task's first switch enters
// its entry function there, which then finds its own toc from r12. ELFv1 has
// no such entry point; a new task's r2 is primed from its entry's descriptor.

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
#if SPPC_ABI_PPC64_ELFV1
  // The symbol names a descriptor, as every ELFv1 function's does: the code,
  // the toc and an environment, which the linker reads to make a call.
  .section ".opd", "aw"
  .p2align 3
_SPPC_GT_SWITCH:
  .quad .L._SPPC_GT_SWITCH, .TOC.@tocbase, 0
  .previous
  .type _SPPC_GT_SWITCH, @function
.L._SPPC_GT_SWITCH:
#else
  .type _SPPC_GT_SWITCH, @function
_SPPC_GT_SWITCH:
#endif
  mflr  0
  mfcr  12
  stdu  1, -544(1)
  std   0, 512(1)
  std   2, 520(1)
  stw   12, 528(1)
  std   14, 32(1)
  std   15, 40(1)
  std   16, 48(1)
  std   17, 56(1)
  std   18, 64(1)
  std   19, 72(1)
  std   20, 80(1)
  std   21, 88(1)
  std   22, 96(1)
  std   23, 104(1)
  std   24, 112(1)
  std   25, 120(1)
  std   26, 128(1)
  std   27, 136(1)
  std   28, 144(1)
  std   29, 152(1)
  std   30, 160(1)
  std   31, 168(1)
  stfd  14, 176(1)
  stfd  15, 184(1)
  stfd  16, 192(1)
  stfd  17, 200(1)
  stfd  18, 208(1)
  stfd  19, 216(1)
  stfd  20, 224(1)
  stfd  21, 232(1)
  stfd  22, 240(1)
  stfd  23, 248(1)
  stfd  24, 256(1)
  stfd  25, 264(1)
  stfd  26, 272(1)
  stfd  27, 280(1)
  stfd  28, 288(1)
  stfd  29, 296(1)
  stfd  30, 304(1)
  stfd  31, 312(1)
#if defined(__ALTIVEC__)
  li    11, 320
  stvx  20, 1, 11
  li    11, 336
  stvx  21, 1, 11
  li    11, 352
  stvx  22, 1, 11
  li    11, 368
  stvx  23, 1, 11
  li    11, 384
  stvx  24, 1, 11
  li    11, 400
  stvx  25, 1, 11
  li    11, 416
  stvx  26, 1, 11
  li    11, 432
  stvx  27, 1, 11
  li    11, 448
  stvx  28, 1, 11
  li    11, 464
  stvx  29, 1, 11
  li    11, 480
  stvx  30, 1, 11
  li    11, 496
  stvx  31, 1, 11
#endif
  mffs  0
  stfd  0, 536(1)

  std   1, 0(3)
  ld    1, 0(4)

  lfd   0, 536(1)
  mtfsf 0xff, 0
#if defined(__ALTIVEC__)
  li    11, 496
  lvx   31, 1, 11
  li    11, 480
  lvx   30, 1, 11
  li    11, 464
  lvx   29, 1, 11
  li    11, 448
  lvx   28, 1, 11
  li    11, 432
  lvx   27, 1, 11
  li    11, 416
  lvx   26, 1, 11
  li    11, 400
  lvx   25, 1, 11
  li    11, 384
  lvx   24, 1, 11
  li    11, 368
  lvx   23, 1, 11
  li    11, 352
  lvx   22, 1, 11
  li    11, 336
  lvx   21, 1, 11
  li    11, 320
  lvx   20, 1, 11
#endif
  lfd   31, 312(1)
  lfd   30, 304(1)
  lfd   29, 296(1)
  lfd   28, 288(1)
  lfd   27, 280(1)
  lfd   26, 272(1)
  lfd   25, 264(1)
  lfd   24, 256(1)
  lfd   23, 248(1)
  lfd   22, 240(1)
  lfd   21, 232(1)
  lfd   20, 224(1)
  lfd   19, 216(1)
  lfd   18, 208(1)
  lfd   17, 200(1)
  lfd   16, 192(1)
  lfd   15, 184(1)
  lfd   14, 176(1)
  ld    31, 168(1)
  ld    30, 160(1)
  ld    29, 152(1)
  ld    28, 144(1)
  ld    27, 136(1)
  ld    26, 128(1)
  ld    25, 120(1)
  ld    24, 112(1)
  ld    23, 104(1)
  ld    22, 96(1)
  ld    21, 88(1)
  ld    20, 80(1)
  ld    19, 72(1)
  ld    18, 64(1)
  ld    17, 56(1)
  ld    16, 48(1)
  ld    15, 40(1)
  ld    14, 32(1)
  lwz   12, 528(1)
  mtcrf 0x38, 12
  ld    2, 520(1)
  ld    12, 512(1)
  mtlr  12
  addi  1, 1, 544
  blr

#if SPPC_ABI_PPC64_ELFV1
  .size _SPPC_GT_SWITCH, . - .L._SPPC_GT_SWITCH
#else
  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH
#endif

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
