#pragma once

/// Switching between tasks. The switch itself is assembly (asm/gt_switch.S),
/// one per calling convention; this lays out a new task's stack so that the
/// first switch into it looks exactly like a resume.

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

#if defined(__x86_64__)
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
#elif defined(__aarch64__)
  // x19-x28, x29, x30, d8-d15 and fpcr, in the order "_gt_switch"
  // stores them. x30 is what "ret" branches to, so the entry point
  // goes there.
  const uintptr_t base = top - 176;
  memset((void*)base, 0, 176);
  *(uint64_t*)(base + 88) = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#else
#  error "the green-thread runtime cannot lay out a stack for this architecture"
#endif
}
