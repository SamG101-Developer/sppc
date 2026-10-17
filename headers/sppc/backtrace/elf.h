#pragma once

/// Crash reporting where executables are ELF and faults arrive as signals:
/// Linux. Frames come from glibc's "backtrace", and names from each loaded
/// object's own symbol table.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/backtrace/common.h>
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>

#define _SPPC_BT_MAX_FRAMES 128
#define _SPPC_BT_MAX_OBJECTS 16
#define _SPPC_BT_ALTSTACK_MIN (64 * 1024)

/// One loaded object's symbol table, mapped from its file on
/// first use and kept for the rest of the (short) life of the
/// process.
typedef struct {
  void const *fbase;
  uintptr_t bias;
  ElfW(Sym) const *syms;
  size_t nsyms;
  char const *strtab;
  size_t strtab_size;
} _sppc_bt_object;

// Defined once, in sppc.c, as for the state in backtrace/common.h.
extern _sppc_bt_object _sppc_bt_objects[_SPPC_BT_MAX_OBJECTS];
extern size_t _sppc_bt_nobjects;
extern void const *_sppc_bt_exe_base;

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

// ==================== SYMBOLS ====================

/// Map the file at @p path and point @p obj at its symbol
/// table: ".symtab" if the file has one, ".dynsym" if it
/// has been stripped. A file that cannot be read or does
/// not look like ELF leaves @p obj with no symbols, and
/// every lookup in it then falls back to "dladdr".
_gnu_cold
inline void _sppc_bt_load(_sppc_bt_object *obj, char const *path) {
  // Open the file, and check that it is a valid file and
  // its size is valid for the format. Simple guards to
  // prevent errors in reporting backtraces.
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) { return; }
  struct stat st;
  if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(ElfW(Ehdr))) {
    close(fd);
    return;
  }

  // Map the file into memory directly, and close the fd.
  // From this point, the file is not needed. Handle a failed
  // mmap too.
  const size_t size = (size_t)st.st_size;
  char const *const map = (char const*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map == MAP_FAILED) { return; }

  // Check the magic bytes are correct, the program header
  // offset + byte len + addr size fits within the file size,
  // and the same for the section header too.
  ElfW(Ehdr) const *const eh = (ElfW(Ehdr) const*)map;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) { return; }
  if (eh->e_phoff + eh->e_phnum * sizeof(ElfW(Phdr)) > size) { return; }
  if (eh->e_shoff == 0 || eh->e_shoff + eh->e_shnum * sizeof(ElfW(Shdr)) > size) { return; }

  // "dladdr" reports where the ELF header is mapped. Its
  // link-time address is that of the segment starting at file
  // offset 0: zero for a PIE or a shared object, the fixed
  // load address for anything else. Set up program header.
  ElfW(Phdr) const *const ph = (ElfW(Phdr) const*)(map + eh->e_phoff);
  uintptr_t header_vaddr = (uintptr_t)0;
  for (size_t i = 0; i < eh->e_phnum; ++i) {
    if (ph[i].p_type == PT_LOAD && ph[i].p_offset == 0) {
      header_vaddr = ph[i].p_vaddr;
      break;
    }
  }
  obj->bias = (uintptr_t)obj->fbase - header_vaddr;

  // Setup the section header, using the symbol table and
  // dynamic symbol table.
  ElfW(Shdr) const *const sh = (ElfW(Shdr) const*)(map + eh->e_shoff);
  for (size_t t = 0; t < 2; ++t) {
    static const unsigned table_types[] = {SHT_SYMTAB, SHT_DYNSYM};
    const unsigned wanted = table_types[t];
    for (size_t i = 0; i < eh->e_shnum; ++i) {
      if (sh[i].sh_type != wanted || sh[i].sh_entsize != sizeof(ElfW(Sym))) { continue; }
      if (sh[i].sh_link >= eh->e_shnum) { continue; }
      ElfW(Shdr) const *const str = &sh[sh[i].sh_link];
      if (sh[i].sh_offset + sh[i].sh_size > size || str->sh_offset + str->sh_size > size) { continue; }
      obj->syms = (ElfW(Sym) const*)(map + sh[i].sh_offset);
      obj->nsyms = sh[i].sh_size / sizeof(ElfW(Sym));
      obj->strtab = map + str->sh_offset;
      obj->strtab_size = str->sh_size;
      return;
    }
  }
}

_gnu_cold
inline _sppc_bt_object* _sppc_bt_object_for(Dl_info const *info) {
  for (size_t i = 0; i < _sppc_bt_nobjects; ++i) {
    if (_sppc_bt_objects[i].fbase == info->dli_fbase) { return &_sppc_bt_objects[i]; }
  }
  if (_sppc_bt_nobjects == _SPPC_BT_MAX_OBJECTS) { return NULL; }

  // The executable is found through "/proc/self/exe", because the name the
  // loader records for it is "argv[0]", which can be relative or anything.
  _sppc_bt_object *const obj = &_sppc_bt_objects[_sppc_bt_nobjects++];
  obj->fbase = info->dli_fbase;
  const bool is_exe = info->dli_fbase == _sppc_bt_exe_base || info->dli_fname == NULL || info->dli_fname[0] != '/';
  _sppc_bt_load(obj, is_exe ? "/proc/self/exe" : info->dli_fname);
  return obj;
}

/// Name the function containing @p lookup_pc, and give its start as a run-time
/// address. Falls back to the nearest exported symbol, and then to nothing.
_gnu_cold
inline char const* _sppc_bt_resolve(void const *lookup_pc, uintptr_t *out_start, char const **out_object) {
  Dl_info info;
  if (dladdr(lookup_pc, &info) == 0) { return NULL; }
  *out_object = info.dli_fname;

  _sppc_bt_object *const obj = _sppc_bt_object_for(&info);
  if (obj != NULL && obj->syms != NULL) {
    const uintptr_t addr = (uintptr_t)lookup_pc - obj->bias;
    ElfW(Sym) const *best = (ElfW(Sym) const*)NULL;
    for (size_t i = 0; i < obj->nsyms; ++i) {
      ElfW(Sym) const *const s = &obj->syms[i];
      const int type = ELF64_ST_TYPE(s->st_info);
      if ((type != STT_FUNC && type != STT_GNU_IFUNC) || s->st_shndx == SHN_UNDEF) { continue; }
      if (s->st_name >= obj->strtab_size || s->st_value > addr) { continue; }
      if (addr < s->st_value + s->st_size) {
        best = s;
        break;
      }
      if (s->st_size == 0 && (best == NULL || s->st_value > best->st_value)) { best = s; }
    }
    if (best != NULL) {
      *out_start = best->st_value + obj->bias;
      return obj->strtab + best->st_name;
    }
  }

  if (info.dli_sname == NULL) { return NULL; }
  *out_start = (uintptr_t)info.dli_saddr;
  return info.dli_sname;
}

// ==================== HANDLER ====================

_gnu_cold
inline void const* _sppc_bt_context_pc(void const *uctx) {
  ucontext_t const *const uc = (ucontext_t const*)uctx;
#if defined(__x86_64__)
  return (void const*)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
  return (void const*)uc->uc_mcontext.pc;
#else
  (void)uc;
  return NULL;
#endif
}

_gnu_cold
inline void _sppc_bt_print(void const *first_pc, const bool first_is_fault) {
  void *frames[_SPPC_BT_MAX_FRAMES];
  const size_t n = (size_t)backtrace(frames, _SPPC_BT_MAX_FRAMES);

  // Start at "first_pc", dropping the reporter's own frames above it: for a
  // signal, the frame it interrupted, whose address is where the fault
  // happened; for a failure reported by call ("_sppc_bt_fail_end"), the
  // return address into the code that failed. Every return address can sit
  // just past the end of its function when the call was the last thing in it
  // (a call to something that does not return always is), so those are
  // looked up one byte back.
  size_t start = (size_t)0;
  for (size_t i = 0; i < n; ++i) {
    if (frames[i] == first_pc) {
      start = i;
      break;
    }
  }

  _sppc_bt_put("stack backtrace:\n");
  for (size_t i = start; i < n; ++i) {
    const uintptr_t pc = (uintptr_t)frames[i];
    const uintptr_t lookup = i == start && first_is_fault && first_pc != NULL ? pc : pc - 1;
    uintptr_t sym_start = (uintptr_t)0;
    char const *object = (char const*)NULL;
    char const *const name = _sppc_bt_resolve((void const*)lookup, &sym_start, &object);

    _sppc_bt_put("  #");
    _sppc_bt_put_dec(i - start);
    _sppc_bt_put(i - start < 10 ? "  " : " ");
    if (name != NULL) {
      _sppc_bt_put_symbol(name);
      _sppc_bt_put("+");
      _sppc_bt_put_hex(pc - sym_start);
    }
    else {
      _sppc_bt_put("?? ");
      _sppc_bt_put_hex(pc);
    }
    if (object != NULL && object[0] != '\0') {
      char *const slash = strrchr(object, '/');
      _sppc_bt_put("  (");
      _sppc_bt_put(slash != NULL ? slash + 1 : object);
      _sppc_bt_put(")");
    }
    _sppc_bt_put("\n");
  }
}

_gnu_cold
inline void _sppc_bt_handler(const int sig, siginfo_t *info, void *uctx) {
  // Only the first fatal signal reports. Any other thread arriving here while
  // it does waits rather than dying: dying ends the process, cutting the
  // report off before it is written. The reporter ends the process for both.
  if (_sppc_atomic_flag_test_and_set(&_sppc_bt_busy)) {
    for (;;) { pause(); }
  }

  _sppc_bt_put("\n[sppc] fatal signal: ");
  _sppc_bt_put(_sppc_bt_signal_name(sig));
  if (sig == SIGSEGV || sig == SIGBUS) {
    _sppc_bt_put(" at address ");
    _sppc_bt_put_hex((uintptr_t)info->si_addr);
  }
  _sppc_bt_put("\n");
  if (_sppc_bt_enabled) { _sppc_bt_print(_sppc_bt_context_pc(uctx), true); }
  else { _sppc_bt_put("note: backtraces are off because SPP_BACKTRACE=0\n"); }
  _sppc_bt_flush();

  // Blocked until the handler returns, then delivered with the default
  // action. Returning alone would do for a fault, which re-executes and
  // faults again, but not for a signal sent from outside.
  signal(sig, SIG_DFL);
  raise(sig);
}

// ==================== FAILURE ====================

/// The status a failure reported by call ends the process with: the one a
/// shell gives a process killed by "SIGABRT", so a script testing for an
/// abort by status still sees one.
#define _SPPC_FAIL_STATUS 134

/// Begin reporting a failure that no signal raised: a failed runtime check,
/// or "sppc_abort". The message goes into the report buffer between this and
/// "_sppc_bt_fail_end". As for a signal, only the first failure reports, and
/// any other thread arriving waits for it to end the process.
_gnu_cold
inline void _sppc_bt_fail_begin(void) {
  if (_sppc_atomic_flag_test_and_set(&_sppc_bt_busy)) {
    for (;;) { pause(); }
  }
}

/// Add a backtrace from "caller_pc", the return address into the code that
/// failed, write the report, and end the process with "_exit": no handler,
/// "atexit" function or stream flush runs, so none can be reached as a gadget
/// from a failure, and nothing is allocated or unwound.
_gnu_cold _gnu_noreturn
inline void _sppc_bt_fail_end(void const *caller_pc) {
  if (_sppc_bt_enabled) { _sppc_bt_print(caller_pc, false); }
  else { _sppc_bt_put("note: backtraces are off because SPP_BACKTRACE=0\n"); }
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
  const long sys_min = sysconf(_SC_SIGSTKSZ);
  if (sys_min > 0 && (size_t)sys_min > size) { size = (size_t)sys_min; }

  void *const p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
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

  char *const env = getenv("SPP_BACKTRACE");
  _sppc_bt_enabled = env == NULL || strcmp(env, "0") != 0;

  // The first "backtrace" loads libgcc_s, which allocates. Done here, the
  // handler never has to.
  void *warm[1];
  backtrace(warm, 1);

  Dl_info exe;
  if (dladdr((void const*)getauxval(AT_PHDR), &exe) != 0) { _sppc_bt_exe_base = exe.dli_fbase; }
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
