// The context switch for the MIPS o32 calling convention (Linux mips and
// mipsel). Saves $s0-$s7, $fp, $gp and $ra, and with a hard float $f20-$f30
// (the even ones, as doubles: under every fp mode o32 has, those hold all
// the callee-saved ones) and fcsr, in a 96-byte frame on the current stack;
// stores the stack pointer through the first argument ($a0), loads the next
// one from the second ($a1), and restores the same block from there.
//
//   0-47  $f20-$f30    48-79  $s0-$s7    80  $fp    84  $gp    88  $ra    92  fcsr
//
// It returns through $t9: a position independent function computes its $gp
// from $t9 on entry, so a new task's first switch can enter its entry
// function as a call would.

  .abicalls
  .set  nomips16
  .set  nomicromips
.text
  .p2align 2
  .globl _SPPC_GT_SWITCH
  // Global so the runtime can call it, hidden so it never leaves the library.
  .hidden _SPPC_GT_SWITCH
  .type  _SPPC_GT_SWITCH, @function

_SPPC_GT_SWITCH:
  addiu $sp, $sp, -96
#if defined(__mips_hard_float)
  sdc1  $f20, 0($sp)
  sdc1  $f22, 8($sp)
  sdc1  $f24, 16($sp)
  sdc1  $f26, 24($sp)
  sdc1  $f28, 32($sp)
  sdc1  $f30, 40($sp)
#endif
  sw    $16, 48($sp)
  sw    $17, 52($sp)
  sw    $18, 56($sp)
  sw    $19, 60($sp)
  sw    $20, 64($sp)
  sw    $21, 68($sp)
  sw    $22, 72($sp)
  sw    $23, 76($sp)
  sw    $30, 80($sp)
  sw    $28, 84($sp)
  sw    $31, 88($sp)
#if defined(__mips_hard_float)
  cfc1  $8, $31
  sw    $8, 92($sp)
#endif

  sw    $sp, 0($4)
  lw    $sp, 0($5)

#if defined(__mips_hard_float)
  lw    $8, 92($sp)
  ctc1  $8, $31
  ldc1  $f30, 40($sp)
  ldc1  $f28, 32($sp)
  ldc1  $f26, 24($sp)
  ldc1  $f24, 16($sp)
  ldc1  $f22, 8($sp)
  ldc1  $f20, 0($sp)
#endif
  lw    $23, 76($sp)
  lw    $22, 72($sp)
  lw    $21, 68($sp)
  lw    $20, 64($sp)
  lw    $19, 60($sp)
  lw    $18, 56($sp)
  lw    $17, 52($sp)
  lw    $16, 48($sp)
  lw    $30, 80($sp)
  lw    $28, 84($sp)
  lw    $31, 88($sp)
  addiu $sp, $sp, 96
  move  $25, $31
  jr    $25

  .size _SPPC_GT_SWITCH, . - _SPPC_GT_SWITCH

// No executable stack wanted.
.section .note.GNU-stack,"",@progbits
