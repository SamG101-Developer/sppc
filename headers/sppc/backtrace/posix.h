#pragma once

/// Crash reporting where faults arrive as signals: Linux, Darwin and
/// FreeBSD. The handler and the alternate stacks are the same on each; the
/// format file included before this one (elf.h, macho.h) supplies the parts
/// that differ, besides the two every platform's printing needs
/// (backtrace/common.h):
///
///   _sppc_bt_secure       whether the process runs setuid/setgid
///   _sppc_bt_prepare      anything to do once, before a handler can run
///   _sppc_bt_context_pc   the faulting pc in a signal's context
///   _sppc_bt_is_cfi_trap  whether a pc is a control-flow-integrity check's trap

#include <sppc/backtrace/common.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>

#define _SPPC_BT_ALTSTACK_MIN (64 * 1024)

extern _sppc_thread_local void *_sppc_bt_altstack;
extern _sppc_thread_local size_t _sppc_bt_altstack_size;

/// Stringify the signal integer with a brief description of
/// the critical failure, which will form the header of the
/// abort's backtrace.
_gnu_cold
inline char const* _sppc_bt_signal_name(const int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic exception)";
    case SIGTRAP: return "SIGTRAP (trap)";
    case SIGABRT: return "SIGABRT (aborted)";
    default: return "unknown signal";
  }
}

// ==================== HANDLER ====================

_gnu_cold _gnu_noreturn
inline void _sppc_bt_cfi_failed(void const *uctx, void const *pc);

_gnu_cold
inline void _sppc_bt_handler(const int sig, siginfo_t *info, void *uctx) {
  // Only the first fatal signal reports. Any other thread arriving here while
  // it does waits rather than dying: dying ends the process, cutting the
  // report off before it is written. The reporter ends the process for both.
  if (_sppc_atomic_flag_test_and_set(&_sppc_bt_busy)) {
    for (;;) { pause(); }
  }

  // A failed control-flow-integrity check (KCFI) traps on an instruction the
  // compiler recorded (SIGILL on x86, SIGTRAP on AArch64). It is reported as
  // the check it is, not as the signal it arrives as.
  void const *const pc = _sppc_bt_context_pc(uctx);
  if ((sig == SIGILL || sig == SIGTRAP) && _sppc_bt_is_cfi_trap(pc)) { _sppc_bt_cfi_failed(uctx, pc); }

  _sppc_bt_put("\n[sppc] fatal signal: ");
  _sppc_bt_put(_sppc_bt_signal_name(sig));
  if (sig == SIGSEGV || sig == SIGBUS) {
    _sppc_bt_put(" at address ");
    _sppc_bt_put_hex((uintptr_t)info->si_addr);
  }
  _sppc_bt_put("\n");
  if (_sppc_bt_enabled) { _sppc_bt_print(uctx, pc); }
  else { _sppc_bt_put(_sppc_bt_off_note); }
  _sppc_bt_flush();

  // Blocked until the handler returns, then delivered with the default
  // action. Returning alone would do for a fault, which re-executes and
  // faults again, but not for a signal sent from outside.
  signal(sig, SIG_DFL);
  raise(sig);
}

// ==================== FAILURE ====================

/// Report a failed control-flow-integrity check, from the handler of the
/// signal its trap raised: the same report and end as a failed runtime check
/// ("sppc_check_failed"), with the backtrace from the call that failed. Which
/// types were expected and found is left out, as nothing in it helps the
/// program's author and all of it helps whoever corrupted the pointer.
_gnu_cold _gnu_noreturn
inline void _sppc_bt_cfi_failed(void const *uctx, void const *pc) {
  _sppc_bt_put("\n[sppc] check failed: control-flow integrity: an indirect call reached a function of another type\n");
  if (_sppc_bt_enabled) { _sppc_bt_print(uctx, pc); }
  else { _sppc_bt_put(_sppc_bt_off_note); }
  _sppc_bt_flush();
  _exit(_SPPC_FAIL_STATUS);
}

// ==================== SETUP ====================

/// Give the calling thread a stack to run the handler on, so a stack overflow
/// can still be reported: the handler cannot run on the stack that just ran
/// out. Every thread needs its own.
_gnu_cold
inline void _sppc_bt_altstack_up(void) {
  if (_sppc_bt_altstack != NULL) { return; }
  size_t size = (size_t)_SPPC_BT_ALTSTACK_MIN;
#if defined(_SC_SIGSTKSZ)
  const long sys_min = sysconf(_SC_SIGSTKSZ);
#else
  const long sys_min = (long)SIGSTKSZ;
#endif
  if (sys_min > 0 && (size_t)sys_min > size) { size = (size_t)sys_min; }

  // "MAP_STACK" is a hint only Linux and the BSDs take.
#if defined(MAP_STACK)
  const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK;
#else
  const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif
  void *const p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (p == MAP_FAILED) { return; }
  const stack_t ss = {.ss_sp = p, .ss_flags = 0, .ss_size = size};
  if (sigaltstack(&ss, NULL) != 0) {
    munmap(p, size);
    return;
  }
  _sppc_bt_altstack = p;
  _sppc_bt_altstack_size = size;
}

_gnu_cold
inline void _sppc_bt_altstack_down(void) {
  if (_sppc_bt_altstack == NULL) { return; }
  const stack_t ss = {.ss_sp = NULL, .ss_flags = SS_DISABLE, .ss_size = 0};
  sigaltstack(&ss, NULL);
  munmap(_sppc_bt_altstack, _sppc_bt_altstack_size);
  _sppc_bt_altstack = NULL;
  _sppc_bt_altstack_size = 0;
}

_gnu_cold
inline void _sppc_bt_install(void) {
  static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};
  const size_t nsignals = sizeof signals / sizeof *signals;

  // A setuid/setgid process trusts nothing in its environment, and does not
  // print its addresses to whoever started it: no backtrace there, whatever
  // SPP_BACKTRACE says, and the variable is not even read.
  const bool secure = _sppc_bt_secure();
  char const *const env = secure ? NULL : getenv("SPP_BACKTRACE");
  _sppc_bt_enabled = !secure && (env == NULL || strcmp(env, "0") != 0);
  if (secure) { _sppc_bt_off_note = "note: backtraces are off in a setuid/setgid process\n"; }

  _sppc_bt_prepare();
  _sppc_bt_altstack_up();

  struct sigaction sa = {0};
  sa.sa_sigaction = _sppc_bt_handler;
  // Not "SA_RESETHAND": a second thread faulting would then take the default
  // action and end the process mid-report. The fatal signals are blocked while
  // the handler runs instead, so one raised inside it still ends the process.
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (size_t i = 0; i < nsignals; ++i) {
    sigaddset(&sa.sa_mask, signals[i]);
  }

  // Only over the default action: a sanitizer or a debugging harness that
  // has put its own handler in place keeps it.
  for (size_t i = 0; i < nsignals; ++i) {
    struct sigaction old;
    if (sigaction(signals[i], NULL, &old) != 0) { continue; }
    if (!(old.sa_flags & SA_SIGINFO) && old.sa_handler == SIG_DFL) { sigaction(signals[i], &sa, NULL); }
  }
}
