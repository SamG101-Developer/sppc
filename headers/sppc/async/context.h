#pragma once

/// Switching between tasks. The switch itself is assembly (asm/gt_switch.S),
/// one per calling convention; this lays out a new task's stack so that the
/// first switch into it looks exactly like a resume.

#include <sppc/config.h>
#include <sppc/async/runtime.h>
#include <string.h>

extern void _gt_switch(void **save_sp, void *const *load_sp);

/// Lay out the stack of the new task, in such a way that the
/// first switch into it looks like a "resume" - the register
/// block "_gt_switch" would have pushed, and below the top of
/// the stack the address it will "ret" to.
_gnu_inline _gnu_nonnull(1, 3)
void* _gt_stack_prime(void *usable, const size_t size, void (*entry)(void)) {
  uintptr_t top = (uintptr_t)usable + size;
  top &= ~(uintptr_t)15;

#if SPPC_ARCH_X86_64
  // From low to high the switch leaves: the sse/x87 control word
  // slot, r15, r14, r13, r12, rbx, rbp, and then the return address.
  // The return address must sit on a 16-byte boundary, because "ret"
  // pops it and SysV wants rsp % 16 == 8 once the callee is entered.
  const uintptr_t ret_slot = top - 16;
  const uintptr_t base = ret_slot - 56;
  memset((void*)base, 0, 56);
  *(uint32_t*)base = 0x1F80u; // mxcsr: all exceptions masked, round to nearest
  *(uint16_t*)(base + 4) = 0x037Fu; // x87 control word: the abi's default
  *(uint64_t*)ret_slot = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ARCH_ARM64
  // x19-x28, x29, x30, d8-d15 and fpcr, in the order "_gt_switch"
  // stores them. x30 is what "ret" branches to, so the entry point
  // goes there.
  const uintptr_t base = top - 176;
  memset((void*)base, 0, 176);
  *(uint64_t*)(base + 88) = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ARCH_X86
  // From low to high the switch leaves: the sse control word slot
  // (kept only when built with sse), the x87 control word, edi,
  // esi, ebx, ebp, and then the return address. The i386 abi wants
  // (esp + 4) % 16 == 0 once the callee is entered, so the return
  // address sits 8 below a 16-byte boundary.
  const uintptr_t ret_slot = top - 8;
  const uintptr_t base = ret_slot - 24;
  memset((void*)base, 0, 24 + 8);
  *(uint32_t*)base = 0x1F80u; // mxcsr: all exceptions masked, round to nearest
  *(uint16_t*)(base + 4) = 0x037Fu; // x87 control word: the abi's default
  *(uint32_t*)ret_slot = (uint32_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_PPC64_ELFV2
  // The 544-byte block "_gt_switch" leaves (its back chain, r14-r31,
  // f14-f31, v20-v31, then lr, r2, cr and fpscr), above which the
  // 32-byte minimum frame the abi gives every callee: the entry saves
  // its lr there, and the zero back chain ends unwinding. The switch
  // branches through r12 to the saved lr, so the entry is reached at
  // its global entry point and sets up its own toc.
  const uintptr_t base = top - 32 - 544;
  memset((void*)base, 0, 544 + 32);
  *(uint64_t*)(base + 512) = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_PPC64_ELFV1
  // The same block, below ELFv1's 112-byte minimum frame, which holds a
  // parameter save area the entry may write. The entry is a descriptor, not
  // code: its first word is the code, which goes in lr, and its second the
  // toc, which goes in r2.
  uint64_t const *const descriptor = (uint64_t const*)(uintptr_t)entry;
  const uintptr_t base = top - 112 - 544;
  memset((void*)base, 0, 544 + 112);
  *(uint64_t*)(base + 512) = descriptor[0];
  *(uint64_t*)(base + 520) = descriptor[1];
  return (void*)base;
#elif SPPC_ABI_AAPCS32
  // From low to high the switch leaves, with a vfp: fpscr and a pad
  // word, d8-d15, then a pad word (r3), r4-r11 and lr; without one,
  // only the last three. lr is what "bx" branches to, so the entry
  // point goes there, its low bit saying whether it is thumb code.
#if defined(__ARM_FP)
  const uintptr_t block = 112;
#else
  const uintptr_t block = 40;
#endif
  const uintptr_t base = top - block;
  memset((void*)base, 0, block);
  *(uint32_t*)(base + block - 4) = (uint32_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_MIPS_O32
  // $f20-$f30 (the even ones, as doubles), $s0-$s7, $fp, $gp, $ra and
  // fcsr, in the order "_gt_switch" stores them, below the 16 bytes
  // o32 has every caller leave for its callee's arguments. $ra is
  // what the switch jumps to, through $t9, which is where a position
  // independent function finds its $gp.
  const uintptr_t base = top - 16 - 96;
  memset((void*)base, 0, 96 + 16);
  *(uint32_t*)(base + 88) = (uint32_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_MIPS_N64
  // $f24-$f31, $s0-$s7, $fp, $gp, $ra and fcsr, in the order
  // "_gt_switch" stores them; n64 wants nothing above the stack
  // pointer. $ra is what the switch jumps to, through $t9, which is
  // where a position independent function finds its $gp.
  const uintptr_t base = top - 160;
  memset((void*)base, 0, 160);
  *(uint64_t*)(base + 144) = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_SPARC_V9
  // Two 176-byte frames, the minimum (a 16-word register save area and
  // 6 words of arguments), each found through a stack pointer biased by
  // -2047. The lower one is "_gt_switch"'s: the window it loads, l0-l7
  // and i0-i7, then fsr. Its i7 is the entry point less 8, as "ret"
  // returns to i7 + 8, and its i6 the upper frame, which the "restore"
  // after it returns into: the trampoline's caller, all zero, so the
  // zero frame pointer ends unwinding.
  const uintptr_t bias = 2047;
  const uintptr_t outer = top - 176;
  const uintptr_t base = outer - 176;
  memset((void*)base, 0, 176 + 176);
  *(uint64_t*)(base + 112) = (uint64_t)(outer - bias);
  *(uint64_t*)(base + 120) = (uint64_t)(uintptr_t)entry - 8;
  return (void*)(base - bias);
#elif SPPC_ABI_M68K_SYSV
  // From low to high the switch leaves, with a 68881: fpcr and
  // fp2-fp7 (12 bytes each); then d2-d7 and a2-a6, and the return
  // address "rts" pops, so the entry point goes there. The word
  // above it is the entry's own return address, which it never uses.
#if defined(__HAVE_68881__)
  const uintptr_t block = 4 + 72 + 44;
#else
  const uintptr_t block = 44;
#endif
  const uintptr_t ret_slot = top - 8;
  const uintptr_t base = ret_slot - block;
  memset((void*)base, 0, block + 8);
  *(uint32_t*)ret_slot = (uint32_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_LOONGARCH_LP64D
  // $ra, $fp, $s0-$s8, $fs0-$fs7 and fcsr0, in the order "_gt_switch"
  // stores them. $ra is what "jr" jumps to, so the entry point goes
  // there; the abi wants nothing above the stack pointer.
  const uintptr_t base = top - 160;
  memset((void*)base, 0, 160);
  *(uint64_t*)base = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ABI_RISCV_LP64D
  // ra, s0-s11, fs0-fs11 and fcsr, in the order "_gt_switch" stores
  // them. ra is what "ret" jumps to, so the entry point goes there;
  // the abi wants nothing above the stack pointer.
  const uintptr_t base = top - 208;
  memset((void*)base, 0, 208);
  *(uint64_t*)base = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif SPPC_ARCH_S390X
  // r6-r14, f8-f15 and the fpc, in the order "_gt_switch" stores
  // them, below the 160-byte save area the abi gives every callee
  // above its stack pointer. r14 is what "br" branches to, so the
  // entry point goes there; the zero back chain ends unwinding.
  const uintptr_t base = top - 160 - 144;
  memset((void*)base, 0, 144 + 160);
  *(uint64_t*)(base + 64) = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#else
#  error "the green-thread runtime cannot lay out a stack for this architecture"
#endif
}
