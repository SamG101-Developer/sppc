// The context switch for the MIPS n64 calling convention (Linux mips64el).
// Saves $s0-$s7, $fp, $gp (callee-saved here, unlike o32) and $ra, and with a
// hard float $f24-$f31 and fcsr, in a 160-byte frame on the current stack;
// stores the stack pointer through the first argument ($a0), loads the next
// one from the second ($a1), and restores the same block from there.
//
//   0-63  $f24-$f31    64-127  $s0-$s7    128  $fp    136  $gp    144  $ra    152  fcsr
//
// It returns through $t9: a position independent function computes its $gp
// from $t9 on entry, so a new task's first switch can enter its entry
// function as a call would.

  .abicalls
  .set  nomips16
  .set  nomicromips
.text
  .p2align 3
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  daddiu $sp, $sp, -160
#if defined(__mips_hard_float)
  sdc1  $f24, 0($sp)
  sdc1  $f25, 8($sp)
  sdc1  $f26, 16($sp)
  sdc1  $f27, 24($sp)
  sdc1  $f28, 32($sp)
  sdc1  $f29, 40($sp)
  sdc1  $f30, 48($sp)
  sdc1  $f31, 56($sp)
#endif
  sd    $16, 64($sp)
  sd    $17, 72($sp)
  sd    $18, 80($sp)
  sd    $19, 88($sp)
  sd    $20, 96($sp)
  sd    $21, 104($sp)
  sd    $22, 112($sp)
  sd    $23, 120($sp)
  sd    $30, 128($sp)
  sd    $28, 136($sp)
  sd    $31, 144($sp)
#if defined(__mips_hard_float)
  cfc1  $8, $31
  sd    $8, 152($sp)
#endif

  sd    $sp, 0($4)
  ld    $sp, 0($5)

#if defined(__mips_hard_float)
  ld    $8, 152($sp)
  ctc1  $8, $31
  ldc1  $f31, 56($sp)
  ldc1  $f30, 48($sp)
  ldc1  $f29, 40($sp)
  ldc1  $f28, 32($sp)
  ldc1  $f27, 24($sp)
  ldc1  $f26, 16($sp)
  ldc1  $f25, 8($sp)
  ldc1  $f24, 0($sp)
#endif
  ld    $23, 120($sp)
  ld    $22, 112($sp)
  ld    $21, 104($sp)
  ld    $20, 96($sp)
  ld    $19, 88($sp)
  ld    $18, 80($sp)
  ld    $17, 72($sp)
  ld    $16, 64($sp)
  ld    $30, 128($sp)
  ld    $28, 136($sp)
  ld    $31, 144($sp)
  daddiu $sp, $sp, 160
  move  $25, $31
  jr    $25

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
