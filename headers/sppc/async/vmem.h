#pragma once

/// Task stacks: mapped from the kernel, with a guard page at the end they
/// grow towards, and a small cache of freed ones so a spawn after an await
/// does not have to map again.

#include <sppc/async/runtime.h>
#include <sys/mman.h>
#include <unistd.h>

_gnu_inline
size_t _gt_page(void) {
  if (_sppc_unlikely(!_gt_R.page)) { _gt_R.page = (size_t)sysconf(_SC_PAGESIZE); }
  return _gt_R.page;
}

// ==================== STACK ====================

_gnu_inline
void* _gt_stack_alloc(const size_t size) {
  if (_gt_R.stack_cached && _gt_R.cached_size == size) {
    return _gt_R.stack_cache[--_gt_R.stack_cached];
  }

  const size_t pg = _gt_page();
  if (!_gt_R.guard) {
    void *const p = mmap(NULL, size, PROT_READ | PROT_WRITE, GT_MAP_FLAGS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
  }

  // The guard goes at the low end, the end a stack grows towards,
  // so running off the bottom of one lands on nothing mapped
  // rather than on the task whose stack the allocator happened to
  // put below it. The usable region therefore starts one page in,
  // and that - not the mapping - is what the caller is given.
  const size_t total = size + pg;
  void *const p = mmap(NULL, total, PROT_NONE, GT_MAP_FLAGS, -1, 0);
  if (p == MAP_FAILED) { return NULL; }
  if (mprotect((char*)p + pg, size, PROT_READ | PROT_WRITE) != 0) {
    munmap(p, total);
    return NULL;
  }
  return (char*)p + pg;
}

/// Hand a stack straight back to the kernel, guard page included.
_gnu_inline
void _gt_stack_unmap(void *usable, const size_t size) {
  const size_t pg = _gt_page();
  if (_gt_R.guard) { munmap((char*)usable - pg, size + pg); }
  else { munmap(usable, size); }
}

_gnu_inline
void _gt_stack_free(void *usable, const size_t size) {
  if (usable == NULL) { return; }
  if (_gt_R.stack_cached < GT_STACK_CACHE &&
    (_gt_R.stack_cached == 0 || _gt_R.cached_size == size)) {
    _gt_R.cached_size = size;
    _gt_R.stack_cache[_gt_R.stack_cached++] = usable;
    return;
  }
  _gt_stack_unmap(usable, size);
}

_gnu_inline
void _gt_set_guard_pages(const int enabled) {
  _gt_init();
  if (!!enabled == !!_gt_R.guard) { return; }
  while (_gt_R.stack_cached) {
    _gt_stack_unmap(_gt_R.stack_cache[--_gt_R.stack_cached], _gt_R.cached_size);
  }
  _gt_R.guard = !!enabled;
}
