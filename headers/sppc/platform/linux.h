#pragma once

/// The parts of the public api that Linux implements with
/// calls of its own, rather than the POSIX ones in
/// platform/posix.h: glibc's barriers, the Linux syscalls
/// ("pipe2", "accept4", "sendfile", "copy_file_range",
/// "getrandom", ...), and process hardening (MDWE, mseal).
/// Darwin defines the same functions in platform/darwin.h.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/platform/posix.h>
#include <link.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>

_Static_assert(
  sizeof(off_t) == sizeof(int64_t), "sppc: off_t is not 64-bit (build with _FILE_OFFSET_BITS=64)");

// ==================== THREADING ====================

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_barrier_init(uint64_t *_sppc_restrict barrier, const uint64_t count) {
  _pthread_handle_alloc(pthread_barrier_t)
  _extract_err(int) pthread_barrier_init(obj, NULL, count);
  _pthread_handle_publish(barrier)
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_wait(uint64_t const *_sppc_restrict barrier) {
  _extract_err(int) pthread_barrier_wait(_pthread_handle(pthread_barrier_t, barrier));
  if (err == PTHREAD_BARRIER_SERIAL_THREAD) { return SPPC_BARRIER_SERIAL_THREAD; }
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_destroy(uint64_t const *_sppc_restrict barrier) {
  pthread_barrier_t *const obj = _pthread_handle(pthread_barrier_t, barrier);
  _extract_err(int) pthread_barrier_destroy(obj);
  free(obj);
  _return_normalized_pthread_err
}

// ==================== SYSCALLS ====================

_posix_syscall(22)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_pipe(int *_sppc_restrict out_read_fd, int *_sppc_restrict out_write_fd) {
  int fds[2];
  _extract_err(int) pipe2(fds, O_CLOEXEC);
  if (err == 0) {
    *out_read_fd = fds[0];
    *out_write_fd = fds[1];
  }
  _return_normalized_err
}

_posix_syscall(40)
_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_sendfile(const int from_fd, const int to_fd, int64_t *offset, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) sendfile(to_fd, from_fd, (off_t*)offset, count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(43)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3) _gnu_nonnull(2, 3)
_sppc_api int sppc_accept(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage, int *_sppc_restrict out_fd) {
  struct sockaddr *const addr = (struct sockaddr*)_sppc_os_sockaddr(out_storage);
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err_async(int, _gt_try_accept(fd, addr, &len, &_async_res), accept4(fd, addr, &len, O_CLOEXEC))
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(75)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fdatasync(const int fd) {
  _extract_err(int) fdatasync(fd);
  _return_normalized_err
}

// "clock_nanosleep" returns its error rather than setting errno, unlike every
// other call routed through "_extract_err_async"; this gives it their shape.
_gnu_inline _gnu_nonnull(3)
int _sppc_clock_nanosleep_errno(const clockid_t clock, const int flags, struct timespec const *duration) {
  const int e = clock_nanosleep(clock, flags, duration, NULL);
  if (e != 0) {
    errno = e;
    return -1;
  }
  return 0;
}

_posix_syscall(230)
_gnu_inline _gnu_restrict_access(read_only, 3) _gnu_nonnull(3)
_sppc_api int sppc_clock_nanosleep(const int clock, const int flags, sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  int os_flags;
  struct timespec ts;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timer_flags(flags, &os_flags) != 0 ||
    _sppc_to_os_timespec(duration, &ts) != 0) {
    return SPPC_EINVAL;
  }
  _extract_err_async(int, _gt_try_sleep(os_clock, os_flags, &ts, &_async_res),
    _sppc_clock_nanosleep_errno(os_clock, os_flags, &ts))
  _return_normalized_err
}

_posix_syscall(318)
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getrandom(const size_t size, char *_sppc_restrict out) {
  size_t filled = 0;
  while (filled < size) {
    const ssize_t n = getrandom(out + filled, size - filled, 0);
    if (n < 0) {
      if (errno == EINTR) { continue; }
      return _sppc_errno(errno);
    }
    filled += (size_t)n;
  }
  return 0;
}

// ==================== STDLIB ====================

_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_copyfile(const int fd_in, const int fd_out, const size_t len, const int flags,
  ssize_t *_sppc_restrict out_n) {
  // No flags are defined yet; the argument keeps room for them.
  if (flags != 0) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  // A partial copy is normal, so the count has to reach the caller.
  _extract_err(ssize_t) copy_file_range(fd_in, NULL, fd_out, NULL, len, 0);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

// ==================== PROCESS HARDENING ====================

// Older headers predate MDWE; the numbers are the kernel's ABI.
#ifndef PR_SET_MDWE
#define PR_SET_MDWE 65
#endif
#ifndef PR_MDWE_REFUSE_EXEC_GAIN
#define PR_MDWE_REFUSE_EXEC_GAIN (1UL << 0)
#endif
#ifndef PR_MDWE_NO_INHERIT
#define PR_MDWE_NO_INHERIT (1UL << 1)
#endif

_gnu_inline _gnu_cold
_sppc_api int sppc_deny_wx(void) {
  if (prctl(PR_SET_MDWE, PR_MDWE_REFUSE_EXEC_GAIN | PR_MDWE_NO_INHERIT, 0L, 0L, 0L) == 0) { return 0; }
  const int err = errno;
  fprintf(stderr,
    "[sppc] could not deny writable-and-executable memory (PR_SET_MDWE needs Linux 6.6+): %s\n"
    "  To run without it, set 'deny_wx = false' under [memory.map] in spp.toml and rebuild.\n",
    strerror(err));
  return _sppc_errno(err);
}

// The generic syscall number, the same on every Linux
// architecture; older headers predate it.
#ifndef SYS_mseal
#define SYS_mseal 462
#endif

// Seal [start, end) rounded out to whole pages; an empty range
// is nothing to seal. Sealing a range twice is not an error.
_gnu_inline _gnu_cold
int _sppc_seal_range(const uintptr_t start, const uintptr_t end, const uintptr_t page) {
  const uintptr_t from = start & ~(page - 1);
  const uintptr_t upto = (end + page - 1) & ~(page - 1);
  if (upto <= from) { return 0; }
  return syscall(SYS_mseal, from, upto - from, 0UL) == 0 ? 0 : errno;
}

// One loaded object: its segments that are not writable
// (code, read-only data) and its RELRO range, which the
// loader has made read-only by now. Writable segments
// stay unsealed: sealing cannot stop a write, MDWE already
// stops them gaining exec, and the brk heap can grow
// beside ".bss". The vdso is the kernel's own and is left
// to it.
_gnu_inline _gnu_cold
int _sppc_seal_object(struct dl_phdr_info *info, const size_t size, void *data) {
  (void)size;
  if (info->dlpi_addr != 0 && info->dlpi_addr == (ElfW(Addr))getauxval(AT_SYSINFO_EHDR)) { return 0; }
  const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
  for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
    ElfW(Phdr) const *const ph = &info->dlpi_phdr[i];
    const uintptr_t start = (uintptr_t)(info->dlpi_addr + ph->p_vaddr);
    int err = 0;
    if (ph->p_type == PT_LOAD && (ph->p_flags & PF_W) == 0) {
      err = _sppc_seal_range(start, start + ph->p_memsz, page);
    }
    else if (ph->p_type == PT_GNU_RELRO) {
      // The loader protects RELRO up to its end rounded down,
      // so the page the end falls in is the writable data
      // after it.
      err = _sppc_seal_range(start, (start + ph->p_memsz) & ~(page - 1), page);
    }
    if (err != 0) {
      *(int*)data = err;
      return 1;
    }
  }
  return 0;
}

// Seal every object loaded at start-up, so its code, constants
// and relocated pointers can never again be unmapped, remapped
// or made writable, not even by a syscall the program is tricked
// into making. Needs Linux 6.10 (mseal); an older kernel is a
// start-up failure that names the key turning it off, never a
// silent no-op.
_gnu_inline _gnu_cold
_sppc_api int sppc_seal_segments(void) {
  int err = 0;
  dl_iterate_phdr(_sppc_seal_object, &err);
  if (err == 0) { return 0; }
  fprintf(stderr,
    "[sppc] could not seal the program's memory mappings (mseal needs Linux 6.10+): %s\n"
    "  To run without it, set 'seal = false' under [memory.map] in spp.toml and rebuild.\n",
    strerror(err));
  return _sppc_errno(err);
}
