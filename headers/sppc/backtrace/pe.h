#pragma once

/// Crash reporting on Windows, where a fault is an exception rather than a
/// signal. An unhandled-exception filter reports it, as the signal handler
/// does elsewhere, and then lets the exception end the process as it would
/// have without one: the exit status (0xc0000005 for an access violation,
/// say) and any crash dump Windows Error Reporting takes are unchanged.
/// "abort" raises SIGABRT in the C library, which is reported the same way.
///
/// The stack is walked with the unwind data every x64 and arm64 function
/// has ("RtlVirtualUnwind"), and named from a PDB where there is one
/// (DbgHelp's), else from the COFF symbol table in the module's file (which
/// mingw keeps unless stripped, and DbgHelp does not read), else from its
/// exports (DbgHelp's again). DbgHelp is loaded from System32 when crash
/// reporting is installed, so that a report never loads a library.
///
/// A stack overflow is reported from a thread of its own, as the stack that
/// overflowed has too little left for DbgHelp; "SetThreadStackGuarantee"
/// keeps enough of each thread's for the filter to start that thread.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/backtrace/common.h>
#include <dbghelp.h>
#include <signal.h>
#include <stdlib.h>

/// What a thread keeps for its exception handlers once its stack has
/// overflowed: enough to start the thread that reports it.
#define _SPPC_BT_STACK_GUARANTEE (64 * 1024)

/// The reporting thread's stack, for DbgHelp reading a PDB.
#define _SPPC_BT_REPORT_STACK (1024 * 1024)

typedef DWORD (WINAPI *_sppc_bt_sym_set_options_fn)(DWORD options);
typedef BOOL (WINAPI *_sppc_bt_sym_initialize_fn)(HANDLE process, PCSTR search_path, BOOL invade);
typedef BOOL (WINAPI *_sppc_bt_sym_from_addr_fn)(HANDLE process, DWORD64 address, PDWORD64 displacement, PSYMBOL_INFO symbol);

// Defined once, in sppc.c.
extern LPTOP_LEVEL_EXCEPTION_FILTER _sppc_bt_win_prev;
extern HMODULE _sppc_bt_win_dbghelp;
extern bool _sppc_bt_win_sym_tried;
extern uintptr_t _sppc_bt_win_stack_lo, _sppc_bt_win_stack_hi;
extern char _sppc_bt_win_object[MAX_PATH * 3];
extern char _sppc_bt_win_name[512];

/// A module whose file's COFF symbols a report has mapped: its base, and the
/// view (NULL for one without any).
#define _SPPC_BT_COFF_MAX 32
typedef struct _sppc_bt_coff {
  void const *base;
  unsigned char const *file;
  size_t size;
} _sppc_bt_coff;
extern _sppc_bt_coff _sppc_bt_win_coff[_SPPC_BT_COFF_MAX];
extern size_t _sppc_bt_win_ncoff;
extern union _sppc_bt_win_symbol {
  SYMBOL_INFO info;
  char room[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
} _sppc_bt_win_symbol;

// ==================== FRAMES ====================

/// The calling thread's stack (a fiber's, on one), where a frame's return
/// address may be read from when its function has no unwind data.
_gnu_cold
inline void _sppc_bt_win_own_stack(void) {
  ULONG_PTR lo = 0, hi = 0;
  GetCurrentThreadStackLimits(&lo, &hi);
  _sppc_bt_win_stack_lo = (uintptr_t)lo;
  _sppc_bt_win_stack_hi = (uintptr_t)hi;
}

/// The return addresses on the stack, from the fault's CONTEXT "uctx", or
/// from here where it is NULL. Each frame is unwound with its function's
/// unwind data (found one byte back from a return address, which can sit
/// just past the end of a function whose last act is a call); a leaf without
/// any returns as it was called, its return address on top of the stack
/// (x64) or in the link register (arm64). The stack the walk reads is
/// "_sppc_bt_win_stack_lo/_hi"'s, the faulting thread's.
// gcc takes "RtlCaptureContext" for a setjmp, which it is not: nothing here
// is clobbered by its "returning twice".
#if SPPC_CC_GCC
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wclobbered"
#endif
_gnu_cold
inline size_t _sppc_bt_frames(void **frames, const size_t max, void const *uctx) {
  CONTEXT ctx;
  if (uctx != NULL) { memcpy(&ctx, uctx, sizeof ctx); }
  else {
    _sppc_bt_win_own_stack();
    RtlCaptureContext(&ctx);
  }
  size_t n = 0;
  while (n < max) {
#if SPPC_ARCH_X86_64
    const DWORD64 pc = ctx.Rip, sp = ctx.Rsp;
#else
    const DWORD64 pc = ctx.Pc, sp = ctx.Sp;
#endif
    if (pc == 0 || sp < _sppc_bt_win_stack_lo || sp >= _sppc_bt_win_stack_hi) { break; }
    frames[n] = (void*)(uintptr_t)pc;
    const DWORD64 lookup = n == 0 && uctx != NULL ? pc : pc - 1;
    ++n;
    DWORD64 image = 0;
    PRUNTIME_FUNCTION const fn = RtlLookupFunctionEntry(lookup, &image, NULL);
    if (fn != NULL) {
      PVOID handler_data;
      DWORD64 establisher;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, pc, fn, &ctx, &handler_data, &establisher, NULL);
    } else {
#if SPPC_ARCH_X86_64
      if (sp + sizeof(DWORD64) > _sppc_bt_win_stack_hi) { break; }
      ctx.Rip = *(DWORD64 const*)(uintptr_t)sp;
      ctx.Rsp = sp + sizeof(DWORD64);
#else
      if (ctx.Lr == pc) { break; }
      ctx.Pc = ctx.Lr;
#endif
    }
#if SPPC_ARCH_X86_64
    if (ctx.Rip == pc && ctx.Rsp == sp) { break; } // no progress: the unwind data ran out
#else
    if (ctx.Pc == pc && ctx.Sp == sp) { break; }
#endif
  }
  return n;
}
#if SPPC_CC_GCC
  #pragma GCC diagnostic pop
#endif

// ==================== SYMBOLS ====================

/// The COFF symbol table of "module"'s file, mapped read-only on first use
/// (a mapping, not the heap, which the fault may have left locked); NULL
/// for a file without one.
_gnu_cold
inline _sppc_bt_coff const* _sppc_bt_win_coff_for(HMODULE module) {
  for (size_t i = 0; i < _sppc_bt_win_ncoff; ++i) {
    if (_sppc_bt_win_coff[i].base == module) { return &_sppc_bt_win_coff[i]; }
  }
  if (_sppc_bt_win_ncoff == _SPPC_BT_COFF_MAX) { return NULL; }
  _sppc_bt_coff *const entry = &_sppc_bt_win_coff[_sppc_bt_win_ncoff++];
  entry->base = module;
  entry->file = NULL;
  wchar_t path[MAX_PATH];
  if (GetModuleFileNameW(module, path, MAX_PATH) == 0) { return entry; }
  HANDLE const file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
    OPEN_EXISTING, 0, NULL);
  if (file == INVALID_HANDLE_VALUE) { return entry; }
  LARGE_INTEGER size;
  HANDLE const section = GetFileSizeEx(file, &size) && size.QuadPart > 0
    ? CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL) : NULL;
  CloseHandle(file);
  if (section == NULL) { return entry; }
  entry->file = (unsigned char const*)MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
  entry->size = (size_t)size.QuadPart;
  CloseHandle(section);
  return entry;
}

/// The function symbol "pc" is in by the COFF symbols of "module"'s file:
/// the nearest external or static function at or before it. Its start in
/// "out_start"; NULL where the file has no symbols, or none before "pc".
_gnu_cold
inline char const* _sppc_bt_win_coff_resolve(HMODULE module, const uintptr_t pc, uintptr_t *out_start) {
  _sppc_bt_coff const *const coff = _sppc_bt_win_coff_for(module);
  if (coff == NULL || coff->file == NULL) { return NULL; }
  unsigned char const *const file = coff->file;
  // The image's sections from its own headers in memory: where each is.
  IMAGE_DOS_HEADER const *const dos = (IMAGE_DOS_HEADER const*)module;
  IMAGE_NT_HEADERS const *const nt = (IMAGE_NT_HEADERS const*)((unsigned char const*)module + dos->e_lfanew);
  IMAGE_SECTION_HEADER const *const sections = IMAGE_FIRST_SECTION(nt);
  const WORD nsections = nt->FileHeader.NumberOfSections;
  const DWORD table = nt->FileHeader.PointerToSymbolTable;
  const DWORD nsyms = nt->FileHeader.NumberOfSymbols;
  if (table == 0 || nsyms == 0 || (uint64_t)table + (uint64_t)nsyms * IMAGE_SIZEOF_SYMBOL + 4 > coff->size) { return NULL; }
  unsigned char const *const strings = file + table + (size_t)nsyms * IMAGE_SIZEOF_SYMBOL;
  const size_t strings_size = coff->size - (size_t)(strings - file);

  uintptr_t best = 0;
  IMAGE_SYMBOL const *found = NULL;
  for (DWORD i = 0; i < nsyms; ++i) {
    IMAGE_SYMBOL sym;
    memcpy(&sym, file + table + (size_t)i * IMAGE_SIZEOF_SYMBOL, sizeof sym); // packed, 18 bytes each
    const DWORD aux = sym.NumberOfAuxSymbols;
    if ((sym.StorageClass == IMAGE_SYM_CLASS_EXTERNAL || sym.StorageClass == IMAGE_SYM_CLASS_STATIC)
      && ISFCN(sym.Type) && sym.SectionNumber > 0 && sym.SectionNumber <= nsections) {
      const uintptr_t at = (uintptr_t)module + sections[sym.SectionNumber - 1].VirtualAddress + sym.Value;
      if (at <= pc && at >= best) {
        best = at;
        found = (IMAGE_SYMBOL const*)(file + table + (size_t)i * IMAGE_SIZEOF_SYMBOL);
      }
    }
    i += aux;
  }
  if (found == NULL) { return NULL; }

  // A short name is in the symbol, eight bytes and not always terminated; a
  // long one is in the string table after the symbols.
  IMAGE_SYMBOL sym;
  memcpy(&sym, found, sizeof sym);
  size_t len = 0;
  if (sym.N.Name.Short != 0) {
    while (len < 8 && sym.N.ShortName[len] != 0) { ++len; }
    memcpy(_sppc_bt_win_name, sym.N.ShortName, len);
  } else {
    const DWORD at = sym.N.Name.Long;
    while (at + len < strings_size && strings[at + len] != 0 && len < sizeof _sppc_bt_win_name - 1) { ++len; }
    memcpy(_sppc_bt_win_name, strings + at, len);
  }
  _sppc_bt_win_name[len] = '\0';
  *out_start = best;
  return _sppc_bt_win_name;
}

/// The function "lookup_pc" is in, and the file of the module it is in. NULL
/// when there is no name for it.
inline char const* _sppc_bt_win_dbghelp_resolve(void const *lookup_pc, uintptr_t *out_start);

_gnu_cold
inline char const* _sppc_bt_resolve(void const *lookup_pc, uintptr_t *out_start, char const **out_object) {
  *out_object = NULL;
  HMODULE module = NULL;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    (LPCWSTR)lookup_pc, &module)) {
    wchar_t wide[MAX_PATH];
    const DWORD len = GetModuleFileNameW(module, wide, MAX_PATH);
    const int got = len == 0 ? 0 : WideCharToMultiByte(CP_UTF8, 0, wide, (int)len, _sppc_bt_win_object,
      (int)sizeof _sppc_bt_win_object - 1, NULL, NULL);
    _sppc_bt_win_object[got > 0 ? got : 0] = '\0';
    if (got > 0) { *out_object = _sppc_bt_win_object; }
  }
  // DbgHelp's name, unless all it has is an export and the file's COFF
  // symbols have one nearer.
  uintptr_t coff_start = 0;
  char const *const coff = module != NULL ? _sppc_bt_win_coff_resolve(module, (uintptr_t)lookup_pc, &coff_start) : NULL;
  uintptr_t help_start = 0;
  char const *const help = _sppc_bt_win_dbghelp_resolve(lookup_pc, &help_start);
  if (help != NULL && (coff == NULL || ((_sppc_bt_win_symbol.info.Flags & SYMFLAG_EXPORT) == 0) || help_start > coff_start)) {
    *out_start = help_start;
    return help;
  }
  *out_start = coff_start;
  return coff;
}

/// The function "lookup_pc" is in by DbgHelp, started on the first lookup
/// (finding each module's symbols only once one is asked for). NULL when it
/// has no name for it.
_gnu_cold
inline char const* _sppc_bt_win_dbghelp_resolve(void const *lookup_pc, uintptr_t *out_start) {
  if (_sppc_bt_win_dbghelp == NULL) { return NULL; }

  _sppc_bt_sym_from_addr_fn from_addr;
  FARPROC const found = GetProcAddress(_sppc_bt_win_dbghelp, "SymFromAddr");
  memcpy(&from_addr, &found, sizeof from_addr);
  if (!_sppc_bt_win_sym_tried) {
    _sppc_bt_win_sym_tried = true;
    _sppc_bt_sym_set_options_fn set_options;
    _sppc_bt_sym_initialize_fn initialize;
    FARPROC const set = GetProcAddress(_sppc_bt_win_dbghelp, "SymSetOptions");
    FARPROC const init = GetProcAddress(_sppc_bt_win_dbghelp, "SymInitialize");
    memcpy(&set_options, &set, sizeof set_options);
    memcpy(&initialize, &init, sizeof initialize);
    if (set_options != NULL) {
      set_options(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_NO_PROMPTS | SYMOPT_FAIL_CRITICAL_ERRORS);
    }
    // A host that started DbgHelp itself has it started already, which
    // this refuses; its symbols serve the same.
    if (initialize != NULL) { initialize(GetCurrentProcess(), NULL, TRUE); }
  }
  if (from_addr == NULL) { return NULL; }
  SYMBOL_INFO *const sym = &_sppc_bt_win_symbol.info;
  memset(sym, 0, sizeof(SYMBOL_INFO));
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  if (!from_addr(GetCurrentProcess(), (DWORD64)(uintptr_t)lookup_pc, &displacement, sym)) { return NULL; }
  *out_start = (uintptr_t)sym->Address;
  return sym->Name;
}

// ==================== EXCEPTIONS ====================

/// What an exception code means, in a few words.
_gnu_cold
inline char const* _sppc_bt_win_exception_name(const DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_INT_OVERFLOW: return "integer overflow";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "floating-point divide by zero";
    case EXCEPTION_FLT_INVALID_OPERATION: return "floating-point invalid operation";
    case EXCEPTION_FLT_OVERFLOW: return "floating-point overflow";
    case EXCEPTION_FLT_UNDERFLOW: return "floating-point underflow";
    case EXCEPTION_FLT_INEXACT_RESULT: return "floating-point inexact result";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "floating-point denormal operand";
    case EXCEPTION_FLT_STACK_CHECK: return "floating-point stack check";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned access";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case EXCEPTION_SINGLE_STEP: return "single step";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "noncontinuable exception";
    case EXCEPTION_INVALID_DISPOSITION: return "invalid disposition";
    case EXCEPTION_GUARD_PAGE: return "guard page";
    case EXCEPTION_INVALID_HANDLE: return "invalid handle";
    case 0xE06D7363: return "unhandled C++ exception";
    default: return "exception";
  }
}

/// The report of the exception "info", from the thread it happened on or
/// the one reporting a stack overflow for it.
_gnu_cold
inline void _sppc_bt_win_report(EXCEPTION_POINTERS const *info) {
  EXCEPTION_RECORD const *const rec = info->ExceptionRecord;
  _sppc_bt_put("\n[sppc] fatal exception: ");
  _sppc_bt_put(_sppc_bt_win_exception_name(rec->ExceptionCode));
  if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
    && rec->NumberParameters >= 2) {
    const ULONG_PTR how = rec->ExceptionInformation[0];
    _sppc_bt_put(how == 0 ? " reading" : how == 1 ? " writing" : how == 8 ? " executing" : " at");
    _sppc_bt_put(" address ");
    _sppc_bt_put_hex((uintptr_t)rec->ExceptionInformation[1]);
  }
  _sppc_bt_put(" (");
  _sppc_bt_put_hex((uintptr_t)rec->ExceptionCode);
  _sppc_bt_put(")\n");
#if SPPC_ARCH_X86_64
  void const *const pc = (void const*)(uintptr_t)info->ContextRecord->Rip;
#else
  void const *const pc = (void const*)(uintptr_t)info->ContextRecord->Pc;
#endif
  if (_sppc_bt_enabled) { _sppc_bt_print(info->ContextRecord, pc, true); }
  else { _sppc_bt_put(_sppc_bt_off_note); }
  _sppc_bt_flush();
}

/// The thread a stack overflow is reported from.
_gnu_cold
inline DWORD WINAPI _sppc_bt_win_report_thread(LPVOID info) {
  _sppc_bt_win_report((EXCEPTION_POINTERS const*)info);
  return 0;
}

/// The unhandled-exception filter. A filter there before it (mingw's C
/// library turning faults into signals for "signal" handlers, a crash-dump
/// tool) goes first, and one that handled the exception ends it here; any
/// other is reported, by the first thread only (others wait, as dying would
/// end the process mid-report), and then left to end the process as it
/// would have.
_gnu_cold
inline LONG WINAPI _sppc_bt_win_filter(EXCEPTION_POINTERS *info) {
  LONG result = EXCEPTION_CONTINUE_SEARCH;
  if (_sppc_bt_win_prev != NULL) {
    result = _sppc_bt_win_prev(info);
    if (result == EXCEPTION_CONTINUE_EXECUTION) { return result; }
  }
  if (_sppc_atomic_flag_test_and_set(&_sppc_bt_busy)) {
    for (;;) { Sleep(INFINITE); }
  }
  _sppc_bt_win_own_stack();
  HANDLE const reporter = info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW
    ? CreateThread(NULL, _SPPC_BT_REPORT_STACK, _sppc_bt_win_report_thread, info, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL)
    : NULL;
  if (reporter != NULL) {
    WaitForSingleObject(reporter, INFINITE);
    CloseHandle(reporter);
  } else {
    _sppc_bt_win_report(info);
  }
  return result;
}

/// The C library's SIGABRT, which "abort" raises: reported from where it
/// was raised, then left to the C library, which ends the process as it
/// would have (having put the default action back before calling this).
_gnu_cold
inline void __cdecl _sppc_bt_win_on_abort(int sig) {
  (void)sig;
  if (_sppc_atomic_flag_test_and_set(&_sppc_bt_busy)) {
    for (;;) { Sleep(INFINITE); }
  }
  _sppc_bt_put("\n[sppc] fatal signal: SIGABRT (aborted)\n");
  if (_sppc_bt_enabled) { _sppc_bt_print(NULL, _sppc_return_address(), true); }
  else { _sppc_bt_put(_sppc_bt_off_note); }
  _sppc_bt_flush();
}

// ==================== SETUP ====================

/// Keep room on the calling thread's stack for its exception handlers once
/// it has overflowed. Every thread needs its own; the room is part of the
/// stack, so there is nothing to give back.
_gnu_cold
inline void _sppc_bt_altstack_up(void) {
  ULONG size = _SPPC_BT_STACK_GUARANTEE;
  SetThreadStackGuarantee(&size);
}

_gnu_inline
void _sppc_bt_altstack_down(void) {}

_gnu_cold
inline void _sppc_bt_install(void) {
  char env[4];
  const DWORD len = GetEnvironmentVariableA("SPP_BACKTRACE", env, sizeof env);
  _sppc_bt_enabled = !(len == 1 && env[0] == '0');
  if (_sppc_bt_enabled && _sppc_bt_win_dbghelp == NULL) {
    _sppc_bt_win_dbghelp = LoadLibraryExW(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  _sppc_bt_altstack_up();

  // Installed again (a second "sppc_init"), the filter keeps the one it
  // found the first time rather than calling itself.
  const LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(_sppc_bt_win_filter);
  if (prev != _sppc_bt_win_filter) { _sppc_bt_win_prev = prev; }

  // Only over the default action, as on POSIX: a handler the program put
  // there keeps it.
  void (__cdecl *const old)(int) = signal(SIGABRT, _sppc_bt_win_on_abort);
  if (old != SIG_DFL && old != _sppc_bt_win_on_abort && old != SIG_ERR) { signal(SIGABRT, old); }
}
