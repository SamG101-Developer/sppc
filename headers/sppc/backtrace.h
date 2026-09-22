#pragma once

/// Crash reporting. A handler on the fatal signals prints which signal it was
/// and a backtrace, then lets the signal kill the process exactly as it would
/// have without one, so the exit status and any core dump are unchanged.
///
/// The walk is glibc's "backtrace", which follows ".eh_frame"; the compiler
/// gives every s++ function an entry there for this. The names come out of
/// each object's own ".symtab" rather than "dladdr", because every s++
/// function has internal linkage and "dladdr" only sees exported symbols.
///
/// A handler may only call what is async-signal-safe. Everything here is,
/// apart from "backtrace" (whose one unsafe step, loading libgcc_s, is done
/// ahead of time at install) and "dladdr" (which reads the link map and does
/// not allocate).

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <unistd.h>

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

static _sppc_bt_object _sppc_bt_objects[_SPPC_BT_MAX_OBJECTS];
static size_t _sppc_bt_nobjects = 0;
static void const *_sppc_bt_exe_base = NULL;
static bool _sppc_bt_enabled = true;
static atomic_flag _sppc_bt_busy = ATOMIC_FLAG_INIT;

static _Thread_local void *_sppc_bt_altstack = NULL;
static _Thread_local size_t _sppc_bt_altstack_size = 0;

static const int _sppc_bt_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};

// ==================== OUTPUT ====================

// The report is built up here and written in one call, so that other threads
// still printing cannot land between its pieces. Only the first fatal signal
// reports ("_sppc_bt_busy"), so one static buffer is enough; a report longer
// than it is cut off rather than split.
static char _sppc_bt_buf[64 * 1024];
static size_t _sppc_bt_len = 0;

static void _sppc_bt_put_n(char const *s, size_t n) {
  const auto room = sizeof _sppc_bt_buf - _sppc_bt_len;
  const auto take = n < room ? n : room;
  memcpy(_sppc_bt_buf + _sppc_bt_len, s, take);
  _sppc_bt_len += take;
}

static void _sppc_bt_put(char const *s) {
  _sppc_bt_put_n(s, strlen(s));
}

/// An s++ symbol ends in "$h" and a 16 hex digit hash of its signature, which
/// keeps overloads apart but is noise in a backtrace. Anything else is printed
/// whole.
static void _sppc_bt_put_symbol(char const *name) {
  const auto n = strlen(name);
  if (n >= 18 && name[n - 18] == '$' && name[n - 17] == 'h') {
    auto all_hex = true;
    for (auto i = n - 16; i < n; ++i) {
      const auto c = name[i];
      all_hex = all_hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    if (all_hex) {
      _sppc_bt_put_n(name, n - 18);
      return;
    }
  }
  _sppc_bt_put(name);
}

static void _sppc_bt_flush(void) {
  auto done = (size_t)0;
  while (done < _sppc_bt_len) {
    const auto w = write(STDERR_FILENO, _sppc_bt_buf + done, _sppc_bt_len - done);
    if (w < 0 && errno == EINTR) { continue; }
    if (w <= 0) { break; }
    done += (size_t)w;
  }
  _sppc_bt_len = 0;
}

static void _sppc_bt_put_hex(uintptr_t v) {
  char buf[2 + 2 * sizeof v + 1];
  auto i = sizeof buf - 1;
  buf[i] = '\0';
  do {
    buf[--i] = "0123456789abcdef"[v & 0xf];
    v >>= 4;
  }
  while (v != 0);
  buf[--i] = 'x';
  buf[--i] = '0';
  _sppc_bt_put(buf + i);
}

static void _sppc_bt_put_dec(size_t v) {
  char buf[21];
  auto i = sizeof buf - 1;
  buf[i] = '\0';
  do {
    buf[--i] = (char)('0' + v % 10);
    v /= 10;
  }
  while (v != 0);
  _sppc_bt_put(buf + i);
}

/// Stringify the signal integer with a brief description of
/// the critical failure, which will form the header of the
/// abort's backtrace.
static char const* _sppc_bt_signal_name(const int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGILL: return "SIGILL (illegal instruction - an overflow or bounds trap, usually)";
    case SIGFPE: return "SIGFPE (arithmetic exception)";
    case SIGTRAP: return "SIGTRAP (trap - an overflow or bounds trap, usually)";
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
static void _sppc_bt_load(_sppc_bt_object *obj, char const *path) {
  // Open the file, and check that it is a valid file and
  // its size is valid for the format. Simple guards to
  // prevent errors in reporting backtraces.
  const auto fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) { return; }
  struct stat st;
  if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(ElfW(Ehdr))) {
    close(fd);
    return;
  }

  // Map the file into memory directly, and close the fd.
  // From this point, the file is not needed. Handle a failed
  // mmap too.
  const auto size = (size_t)st.st_size;
  const auto map = (char const*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map == MAP_FAILED) { return; }

  // Check the magic bytes are correct, the program header
  // offset + byte len + addr size fits within the file size,
  // and the same for the section header too.
  const auto eh = (ElfW(Ehdr) const*)map;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) { return; }
  if (eh->e_phoff + eh->e_phnum * sizeof(ElfW(Phdr)) > size) { return; }
  if (eh->e_shoff == 0 || eh->e_shoff + eh->e_shnum * sizeof(ElfW(Shdr)) > size) { return; }

  // "dladdr" reports where the ELF header is mapped. Its
  // link-time address is that of the segment starting at file
  // offset 0: zero for a PIE or a shared object, the fixed
  // load address for anything else. Set up program header.
  const auto ph = (ElfW(Phdr) const*)(map + eh->e_phoff);
  auto header_vaddr = (uintptr_t)0;
  for (size_t i = 0; i < eh->e_phnum; ++i) {
    if (ph[i].p_type == PT_LOAD && ph[i].p_offset == 0) {
      header_vaddr = ph[i].p_vaddr;
      break;
    }
  }
  obj->bias = (uintptr_t)obj->fbase - header_vaddr;

  // Setup the section header, using the symbol table and
  // dynamic symbol table.
  const auto sh = (ElfW(Shdr) const*)(map + eh->e_shoff);
  for (size_t t = 0; t < 2; ++t) {
    constexpr unsigned table_types[] = {SHT_SYMTAB, SHT_DYNSYM};
    const auto wanted = table_types[t];
    for (size_t i = 0; i < eh->e_shnum; ++i) {
      if (sh[i].sh_type != wanted || sh[i].sh_entsize != sizeof(ElfW(Sym))) { continue; }
      if (sh[i].sh_link >= eh->e_shnum) { continue; }
      const auto str = &sh[sh[i].sh_link];
      if (sh[i].sh_offset + sh[i].sh_size > size || str->sh_offset + str->sh_size > size) { continue; }
      obj->syms = (ElfW(Sym) const*)(map + sh[i].sh_offset);
      obj->nsyms = sh[i].sh_size / sizeof(ElfW(Sym));
      obj->strtab = map + str->sh_offset;
      obj->strtab_size = str->sh_size;
      return;
    }
  }
}

static _sppc_bt_object* _sppc_bt_object_for(Dl_info const *info) {
  for (size_t i = 0; i < _sppc_bt_nobjects; ++i) {
    if (_sppc_bt_objects[i].fbase == info->dli_fbase) { return &_sppc_bt_objects[i]; }
  }
  if (_sppc_bt_nobjects == _SPPC_BT_MAX_OBJECTS) { return NULL; }

  // The executable is found through "/proc/self/exe", because the name the
  // loader records for it is "argv[0]", which can be relative or anything.
  const auto obj = &_sppc_bt_objects[_sppc_bt_nobjects++];
  obj->fbase = info->dli_fbase;
  const auto is_exe = info->dli_fbase == _sppc_bt_exe_base || info->dli_fname == NULL || info->dli_fname[0] != '/';
  _sppc_bt_load(obj, is_exe ? "/proc/self/exe" : info->dli_fname);
  return obj;
}

/// Name the function containing @p lookup_pc, and give its start as a run-time
/// address. Falls back to the nearest exported symbol, and then to nothing.
static char const* _sppc_bt_resolve(void const *lookup_pc, uintptr_t *out_start, char const **out_object) {
  Dl_info info;
  if (dladdr(lookup_pc, &info) == 0) { return NULL; }
  *out_object = info.dli_fname;

  const auto obj = _sppc_bt_object_for(&info);
  if (obj != NULL && obj->syms != NULL) {
    const auto addr = (uintptr_t)lookup_pc - obj->bias;
    auto best = (ElfW(Sym) const*)NULL;
    for (size_t i = 0; i < obj->nsyms; ++i) {
      const auto s = &obj->syms[i];
      const auto type = ELF64_ST_TYPE(s->st_info);
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

static void const* _sppc_bt_context_pc(void const *uctx) {
  const auto uc = (ucontext_t const*)uctx;
#if defined(__x86_64__)
  return (void const*)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
  return (void const*)uc->uc_mcontext.pc;
#else
  (void)uc;
  return NULL;
#endif
}

static void _sppc_bt_print(void const *fault_pc) {
  void *frames[_SPPC_BT_MAX_FRAMES];
  const auto n = (size_t)backtrace(frames, _SPPC_BT_MAX_FRAMES);

  // Start at the frame the signal interrupted, dropping the handler and the
  // kernel's return trampoline above it. That frame's address is where the
  // fault happened; every one below it is a return address, which can sit
  // just past the end of its function when the call was the last thing in it
  // (a call to something that does not return always is), so those are
  // looked up one byte back.
  auto start = (size_t)0;
  for (size_t i = 0; i < n; ++i) {
    if (frames[i] == fault_pc) {
      start = i;
      break;
    }
  }

  _sppc_bt_put("stack backtrace:\n");
  for (auto i = start; i < n; ++i) {
    const auto pc = (uintptr_t)frames[i];
    const auto lookup = i == start && fault_pc != NULL ? pc : pc - 1;
    auto sym_start = (uintptr_t)0;
    auto object = (char const*)NULL;
    const auto name = _sppc_bt_resolve((void const*)lookup, &sym_start, &object);

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
      const auto slash = strrchr(object, '/');
      _sppc_bt_put("  (");
      _sppc_bt_put(slash != NULL ? slash + 1 : object);
      _sppc_bt_put(")");
    }
    _sppc_bt_put("\n");
  }
}

static void _sppc_bt_handler(const int sig, siginfo_t *info, void *uctx) {
  // Only the first fatal signal reports. Any other thread arriving here while
  // it does waits rather than dying: dying ends the process, cutting the
  // report off before it is written. The reporter ends the process for both.
  if (atomic_flag_test_and_set(&_sppc_bt_busy)) {
    for (;;) { pause(); }
  }

  _sppc_bt_put("\n[sppc] fatal signal: ");
  _sppc_bt_put(_sppc_bt_signal_name(sig));
  if (sig == SIGSEGV || sig == SIGBUS) {
    _sppc_bt_put(" at address ");
    _sppc_bt_put_hex((uintptr_t)info->si_addr);
  }
  _sppc_bt_put("\n");
  if (_sppc_bt_enabled) { _sppc_bt_print(_sppc_bt_context_pc(uctx)); }
  else { _sppc_bt_put("note: backtraces are off because SPP_BACKTRACE=0\n"); }
  _sppc_bt_flush();

  // Blocked until the handler returns, then delivered with the default
  // action. Returning alone would do for a fault, which re-executes and
  // faults again, but not for a signal sent from outside.
  signal(sig, SIG_DFL);
  raise(sig);
}

// ==================== SETUP ====================

/// Give the calling thread a stack to run the handler on, so a stack overflow
/// can still be reported: the handler cannot run on the stack that just ran
/// out. Every thread needs its own.
static void _sppc_bt_altstack_up(void) {
  if (_sppc_bt_altstack != NULL) { return; }
  auto size = (size_t)_SPPC_BT_ALTSTACK_MIN;
  const auto sys_min = sysconf(_SC_SIGSTKSZ);
  if (sys_min > 0 && (size_t)sys_min > size) { size = (size_t)sys_min; }

  const auto p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (p == MAP_FAILED) { return; }
  const stack_t ss = {.ss_sp = p, .ss_flags = 0, .ss_size = size};
  if (sigaltstack(&ss, NULL) != 0) {
    munmap(p, size);
    return;
  }
  _sppc_bt_altstack = p;
  _sppc_bt_altstack_size = size;
}

static void _sppc_bt_altstack_down(void) {
  if (_sppc_bt_altstack == NULL) { return; }
  const stack_t ss = {.ss_sp = NULL, .ss_flags = SS_DISABLE, .ss_size = 0};
  sigaltstack(&ss, NULL);
  munmap(_sppc_bt_altstack, _sppc_bt_altstack_size);
  _sppc_bt_altstack = NULL;
  _sppc_bt_altstack_size = 0;
}

static void _sppc_bt_install(void) {
  const auto env = getenv("SPP_BACKTRACE");
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
  for (size_t i = 0; i < sizeof _sppc_bt_signals / sizeof *_sppc_bt_signals; ++i) {
    sigaddset(&sa.sa_mask, _sppc_bt_signals[i]);
  }

  // Only over the default action: a sanitizer or a debugging harness that
  // has put its own handler in place keeps it.
  for (size_t i = 0; i < sizeof _sppc_bt_signals / sizeof *_sppc_bt_signals; ++i) {
    struct sigaction old;
    if (sigaction(_sppc_bt_signals[i], NULL, &old) != 0) { continue; }
    if (!(old.sa_flags & SA_SIGINFO) && old.sa_handler == SIG_DFL) { sigaction(_sppc_bt_signals[i], &sa, NULL); }
  }
}
