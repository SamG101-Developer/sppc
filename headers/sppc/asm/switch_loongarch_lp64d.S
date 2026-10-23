// The context switch for the LoongArch LP64D calling convention (Linux
// loongarch64). Saves $ra, $fp, $s0-$s8, $fs0-$fs7 and fcsr0 in a 160-byte
// frame on the current stack, stores the stack pointer through the first
// argument ($a0), loads the next one from the second ($a1), and restores the
// same block from there. $tp holds the thread pointer and $r21 is reserved,
// so neither is touched.

.text
  .p2align 4
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  addi.d $sp, $sp, -160
  st.d   $ra, $sp, 0
  st.d   $fp, $sp, 8
  st.d   $s0, $sp, 16
  st.d   $s1, $sp, 24
  st.d   $s2, $sp, 32
  st.d   $s3, $sp, 40
  st.d   $s4, $sp, 48
  st.d   $s5, $sp, 56
  st.d   $s6, $sp, 64
  st.d   $s7, $sp, 72
  st.d   $s8, $sp, 80
  fst.d  $fs0, $sp, 88
  fst.d  $fs1, $sp, 96
  fst.d  $fs2, $sp, 104
  fst.d  $fs3, $sp, 112
  fst.d  $fs4, $sp, 120
  fst.d  $fs5, $sp, 128
  fst.d  $fs6, $sp, 136
  fst.d  $fs7, $sp, 144
  movfcsr2gr $t0, $fcsr0
  st.d   $t0, $sp, 152

  st.d   $sp, $a0, 0
  ld.d   $sp, $a1, 0

  ld.d   $t0, $sp, 152
  movgr2fcsr $fcsr0, $t0
  fld.d  $fs7, $sp, 144
  fld.d  $fs6, $sp, 136
  fld.d  $fs5, $sp, 128
  fld.d  $fs4, $sp, 120
  fld.d  $fs3, $sp, 112
  fld.d  $fs2, $sp, 104
  fld.d  $fs1, $sp, 96
  fld.d  $fs0, $sp, 88
  ld.d   $s8, $sp, 80
  ld.d   $s7, $sp, 72
  ld.d   $s6, $sp, 64
  ld.d   $s5, $sp, 56
  ld.d   $s4, $sp, 48
  ld.d   $s3, $sp, 40
  ld.d   $s2, $sp, 32
  ld.d   $s1, $sp, 24
  ld.d   $s0, $sp, 16
  ld.d   $fp, $sp, 8
  ld.d   $ra, $sp, 0
  addi.d $sp, $sp, 160
  jr     $ra

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
