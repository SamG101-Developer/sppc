#pragma once

/// Crash reporting's format half where executables are Mach-O: macOS. Names
/// come from each loaded image's own symbol table, which (unlike ELF's
/// ".symtab") is mapped with the image, in "__LINKEDIT", so nothing is read
/// from a file. Frames come from walking the frame-pointer chain, which both
/// Apple ABIs keep in every function. The signal half is backtrace/posix.h.

#include <sppc/backtrace/common.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/ucontext.h>

// ==================== SYMBOLS ====================

/// What a lookup needs of one loaded image.
typedef struct {
  uintptr_t slide;
  struct nlist_64 const *syms;
  size_t nsyms;
  char const *strtab;
  size_t strtab_size;
  unsigned sect;
} _sppc_bt_image;

/// Find the image's slide, symbol table and string table from its load
/// commands, and the 1-based ordinal (as "n_sect" counts) of the section
/// holding @p pc. False if any is missing or out of place.
_gnu_cold
inline bool _sppc_bt_image_for(struct mach_header_64 const *mh, const uintptr_t pc, _sppc_bt_image *out) {
  if (mh->magic != MH_MAGIC_64) { return false; }
  struct segment_command_64 const *text = (struct segment_command_64 const*)NULL;
  struct segment_command_64 const *linkedit = (struct segment_command_64 const*)NULL;
  struct symtab_command const *symtab = (struct symtab_command const*)NULL;

  // The load commands follow the header back to back, each giving its own
  // size. Sections are numbered across every segment, in load-command order,
  // from 1; the matching one is picked out once the slide is known.
  char const *cmd = (char const*)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds; ++i) {
    struct load_command const *const lc = (struct load_command const*)cmd;
    if (lc->cmdsize < sizeof *lc) { return false; }
    if (lc->cmd == LC_SEGMENT_64) {
      struct segment_command_64 const *const seg = (struct segment_command_64 const*)lc;
      if (strcmp(seg->segname, SEG_TEXT) == 0) { text = seg; }
      if (strcmp(seg->segname, SEG_LINKEDIT) == 0) { linkedit = seg; }
    }
    else if (lc->cmd == LC_SYMTAB) { symtab = (struct symtab_command const*)lc; }
    cmd += lc->cmdsize;
  }
  if (text == NULL || linkedit == NULL || symtab == NULL) { return false; }

  // How far the image was moved from the addresses it was linked at, and
  // where in memory the start of "__LINKEDIT"'s file range landed: the
  // symbol and string tables are given as file offsets into it.
  out->slide = (uintptr_t)mh - (uintptr_t)text->vmaddr;
  const uint64_t lo = linkedit->fileoff;
  const uint64_t hi = linkedit->fileoff + linkedit->filesize;
  if (symtab->symoff < lo || symtab->symoff + (uint64_t)symtab->nsyms * sizeof(struct nlist_64) > hi) { return false; }
  if (symtab->stroff < lo || symtab->stroff + (uint64_t)symtab->strsize > hi) { return false; }
  const uintptr_t base = out->slide + (uintptr_t)linkedit->vmaddr - (uintptr_t)linkedit->fileoff;
  out->syms = (struct nlist_64 const*)(base + symtab->symoff);
  out->nsyms = symtab->nsyms;
  out->strtab = (char const*)(base + symtab->stroff);
  out->strtab_size = symtab->strsize;

  const uint64_t addr = (uint64_t)(pc - out->slide);
  unsigned ordinal = 0;
  out->sect = 0;
  cmd = (char const*)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds && out->sect == 0; ++i) {
    struct load_command const *const lc = (struct load_command const*)cmd;
    if (lc->cmd == LC_SEGMENT_64) {
      struct segment_command_64 const *const seg = (struct segment_command_64 const*)lc;
      struct section_64 const *const sects = (struct section_64 const*)(seg + 1);
      for (uint32_t j = 0; j < seg->nsects; ++j) {
        ++ordinal;
        if (addr >= sects[j].addr && addr < sects[j].addr + sects[j].size) {
          out->sect = ordinal;
          break;
        }
      }
    }
    cmd += lc->cmdsize;
  }
  return out->sect != 0;
}

/// Name the function containing @p lookup_pc, and give its start as a run-time
/// address. A Mach-O symbol has no size, so it is the nearest one at or before
/// the pc in the same section; local ("static") functions are there too, unless
/// the image was stripped. Falls back to "dladdr" (exported symbols only), and
/// then to nothing.
_gnu_cold
inline char const* _sppc_bt_resolve(void const *lookup_pc, uintptr_t *out_start, char const **out_object) {
  Dl_info info;
  if (dladdr(lookup_pc, &info) == 0) { return NULL; }
  *out_object = info.dli_fname;

  _sppc_bt_image img;
  if (info.dli_fbase != NULL && _sppc_bt_image_for((struct mach_header_64 const*)info.dli_fbase, (uintptr_t)lookup_pc, &img)) {
    const uint64_t addr = (uint64_t)((uintptr_t)lookup_pc - img.slide);
    struct nlist_64 const *best = (struct nlist_64 const*)NULL;
    for (size_t i = 0; i < img.nsyms; ++i) {
      struct nlist_64 const *const s = &img.syms[i];
      // Debugger entries (N_STAB) and anything not defined in a section.
      if ((s->n_type & N_STAB) != 0 || (s->n_type & N_TYPE) != N_SECT || s->n_sect != img.sect) { continue; }
      if (s->n_un.n_strx == 0 || s->n_un.n_strx >= img.strtab_size || s->n_value > addr) { continue; }
      if (best == NULL || s->n_value > best->n_value) { best = s; }
    }
    if (best != NULL) {
      // The C name, without the underscore Mach-O puts in front of every one.
      char const *const name = img.strtab + best->n_un.n_strx;
      *out_start = (uintptr_t)best->n_value + img.slide;
      return name[0] == '_' ? name + 1 : name;
    }
  }

  // "dladdr" has already dropped the underscore.
  if (info.dli_sname == NULL) { return NULL; }
  *out_start = (uintptr_t)info.dli_saddr;
  return info.dli_sname;
}

// ==================== STACK ====================

_gnu_cold
inline bool _sppc_bt_secure(void) {
  return issetugid() != 0;
}

/// Mach-O has no record of where control-flow-integrity checks trap, so a
/// failed one is reported as the signal it arrives as.
_gnu_cold
inline bool _sppc_bt_is_cfi_trap(void const *pc) {
  (void)pc;
  return false;
}

/// Nothing to load ahead of time: the symbol tables are already mapped, and
/// the walk below calls nothing that allocates.
_gnu_cold
inline void _sppc_bt_prepare(void) {}

/// An address with any pointer-authentication bits taken off. A return
/// address saved by the system libraries (built arm64e) is signed, even when
/// the process is plain arm64. "xpaclri" strips x30 in place, and is in the
/// hint space, so it does nothing on a core without pointer authentication.
_gnu_cold
inline uintptr_t _sppc_bt_strip(uintptr_t addr) {
#if defined(__aarch64__)
  register uintptr_t x30 __asm__("x30") = addr;
  __asm__("hint #7" : "+r"(x30)); // xpaclri
  return x30;
#else
  return addr;
#endif
}

/// Copy a frame record (the saved frame pointer, then the return address)
/// through the kernel, which fails rather than faults on an address that is
/// not mapped: the chain being walked may be what the crash broke.
_gnu_cold
inline bool _sppc_bt_read_frame(const uintptr_t fp, uintptr_t out[2]) {
  mach_vm_size_t got = 0;
  const kern_return_t kr = mach_vm_read_overwrite(
    mach_task_self(), (mach_vm_address_t)fp, (mach_vm_size_t)(2 * sizeof(uintptr_t)),
    (mach_vm_address_t)(uintptr_t)out, &got);
  return kr == KERN_SUCCESS && got == 2 * sizeof(uintptr_t);
}

/// Darwin's own "backtrace" walks the same chain, but from the handler: the
/// interrupted function's pc is never a return address, so it is missing, and
/// the walk stops at the end of the thread's pthread stack, so a green
/// thread's is never followed. Walked here from the signal's context instead:
/// the pc where the fault happened, then each frame record from its frame
/// pointer. Without a context (a failure reported by call), from this
/// function's own frame.
_gnu_cold
inline size_t _sppc_bt_frames(void **frames, const size_t max, void const *uctx) {
  size_t n = (size_t)0;
  uintptr_t fp;
  if (uctx != NULL) {
    _STRUCT_MCONTEXT64 const *const mc = ((ucontext_t const*)uctx)->uc_mcontext;
#if defined(__x86_64__)
    frames[n++] = (void*)(uintptr_t)mc->__ss.__rip;
    fp = (uintptr_t)mc->__ss.__rbp;
#elif defined(__aarch64__)
    const uintptr_t pc = _sppc_bt_strip((uintptr_t)__darwin_arm_thread_state64_get_pc(mc->__ss));
    const uintptr_t lr = _sppc_bt_strip((uintptr_t)__darwin_arm_thread_state64_get_lr(mc->__ss));
    fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(mc->__ss);
    frames[n++] = (void*)pc;

    // A leaf function, or one stopped before it has saved its frame record,
    // holds its caller's return address only in lr: the chain from fp skips
    // straight to the caller's caller. lr is a frame of its own when it is
    // not that next return address, and not back into the faulting function
    // (where it points once anything called from it has returned).
    uintptr_t rec[2];
    const uintptr_t next = _sppc_bt_read_frame(fp, rec) ? _sppc_bt_strip(rec[1]) : (uintptr_t)0;
    uintptr_t pc_start = (uintptr_t)0, lr_start = (uintptr_t)0;
    char const *object;
    char const *const pc_name = _sppc_bt_resolve((void const*)pc, &pc_start, &object);
    char const *const lr_name = _sppc_bt_resolve((void const*)(lr - 1), &lr_start, &object);
    const bool same = pc_name != NULL && lr_name != NULL && pc_start == lr_start;
    if (lr != 0 && lr != next && !same && n < max) { frames[n++] = (void*)lr; }
#else
    return n;
#endif
  }
  else {
    fp = (uintptr_t)__builtin_frame_address(0);
  }

  // Stacks grow down, so each record is above the last; a chain that does
  // not climb is broken, and ends the walk, as does the zero frame pointer
  // the outermost frame of every stack has.
  while (n < max && fp != 0 && fp % sizeof(uintptr_t) == 0) {
    uintptr_t rec[2];
    if (!_sppc_bt_read_frame(fp, rec)) { break; }
    const uintptr_t ret = _sppc_bt_strip(rec[1]);
    if (ret == 0) { break; }
    frames[n++] = (void*)ret;
    if (rec[0] <= fp) { break; }
    fp = rec[0];
  }
  return n;
}

_gnu_cold
inline void const* _sppc_bt_context_pc(void const *uctx) {
  _STRUCT_MCONTEXT64 const *const mc = ((ucontext_t const*)uctx)->uc_mcontext;
#if defined(__x86_64__)
  return (void const*)(uintptr_t)mc->__ss.__rip;
#elif defined(__aarch64__)
  return (void const*)_sppc_bt_strip((uintptr_t)__darwin_arm_thread_state64_get_pc(mc->__ss));
#else
  (void)mc;
  return NULL;
#endif
}
