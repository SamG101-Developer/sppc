#pragma once

/// Crash reporting's format half where executables are ELF: Linux. Frames
/// come from glibc's "backtrace", and names from each loaded object's own
/// symbol table, read from its file. The signal half is backtrace/posix.h.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/backtrace/common.h>
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>

#define _SPPC_BT_MAX_OBJECTS 16

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
  // The addresses of the object's control-flow-integrity traps
  // (".kcfi_traps"): one 32-bit offset per trap, from the entry
  // to the trap instruction. None without the section.
  char const *cfi_traps;
  uintptr_t cfi_traps_vaddr;
  size_t ncfi_traps;
} _sppc_bt_object;

// Defined once, in sppc.c, as for the state in backtrace/common.h.
extern _sppc_bt_object _sppc_bt_objects[_SPPC_BT_MAX_OBJECTS];
extern size_t _sppc_bt_nobjects;
extern void const *_sppc_bt_exe_base;

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

  // The compiler's record of where its control-flow-integrity
  // checks trap, found by name.
  if (eh->e_shstrndx < eh->e_shnum && sh[eh->e_shstrndx].sh_offset + sh[eh->e_shstrndx].sh_size <= size) {
    ElfW(Shdr) const *const names = &sh[eh->e_shstrndx];
    for (size_t i = 0; i < eh->e_shnum; ++i) {
      if (sh[i].sh_name >= names->sh_size || sh[i].sh_offset + sh[i].sh_size > size) { continue; }
      if (strncmp(map + names->sh_offset + sh[i].sh_name, ".kcfi_traps", names->sh_size - sh[i].sh_name) != 0) {
        continue;
      }
      obj->cfi_traps = map + sh[i].sh_offset;
      obj->cfi_traps_vaddr = sh[i].sh_addr;
      obj->ncfi_traps = sh[i].sh_size / sizeof(int32_t);
      break;
    }
  }

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

/// Whether @p pc is where a control-flow-integrity check traps (KCFI), as the
/// compiler records in its object's ".kcfi_traps". Each entry is the offset from
/// itself to the trap instruction, which is where the signal reports the fault.
_gnu_cold
inline bool _sppc_bt_is_cfi_trap(void const *pc) {
  Dl_info info;
  if (dladdr(pc, &info) == 0) { return false; }
  _sppc_bt_object const *const obj = _sppc_bt_object_for(&info);
  if (obj == NULL || obj->cfi_traps == NULL) { return false; }

  const uintptr_t at = (uintptr_t)pc - obj->bias;
  for (size_t i = 0; i < obj->ncfi_traps; ++i) {
    int32_t offset;
    memcpy(&offset, obj->cfi_traps + i * sizeof offset, sizeof offset);
    if (obj->cfi_traps_vaddr + i * sizeof offset + (uintptr_t)(intptr_t)offset == at) { return true; }
  }
  return false;
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

// ==================== STACK ====================

_gnu_cold
inline bool _sppc_bt_secure(void) {
  return getauxval(AT_SECURE) != 0;
}

/// The first "backtrace" loads libgcc_s, which allocates. Done here, the
/// handler never has to. The executable's base is noted too, so its symbols
/// are read from "/proc/self/exe" rather than from whatever argv[0] was.
_gnu_cold
inline void _sppc_bt_prepare(void) {
  void *warm[1];
  backtrace(warm, 1);

  Dl_info exe;
  if (dladdr((void const*)getauxval(AT_PHDR), &exe) != 0) { _sppc_bt_exe_base = exe.dli_fbase; }
}

/// glibc's unwinder reads each frame's unwind tables, and steps through a
/// signal frame into the code it interrupted, so the context is not needed.
_gnu_cold
inline size_t _sppc_bt_frames(void **frames, const size_t max, void const *uctx) {
  (void)uctx;
  const int n = backtrace(frames, (int)max);
  return n > 0 ? (size_t)n : (size_t)0;
}

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
