#pragma once

/// The parts of the public api that FreeBSD implements with
/// calls of its own, rather than the POSIX ones in
/// platform/posix.h. It has nearly every call Linux uses
/// (platform/linux_freebsd.h); "sendfile" is built on its own,
/// and process hardening has no switch for the running image.
/// Linux and Darwin define the same functions in
/// platform/linux.h and platform/darwin.h.

#include <sppc/platform/posix.h>
#include <sppc/platform/linux_freebsd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

_Static_assert(sizeof(off_t) == sizeof(int64_t), "sppc: off_t is not 64-bit");

// ==================== SYSCALLS ====================

/// Linux's "sendfile" from FreeBSD's, which only sends to a
/// socket, takes the offset by value, never moves the file
/// position, and reads 0 bytes as "to the end of the file".
/// It reports what it sent through "sbytes" even when it fails
/// part way, which counts as sent. Any target other than a
/// socket is a read and a write.
_gnu_inline
ssize_t _sppc_sendfile(const int to_fd, const int from_fd, int64_t *offset, const size_t count) {
  if (count == 0) { return 0; }
  const off_t start = offset != NULL ? (off_t)*offset : lseek(from_fd, 0, SEEK_CUR);
  if (start < 0) { return -1; }

  ssize_t sent;
  off_t sbytes = 0;
  if (sendfile(from_fd, to_fd, start, count, NULL, &sbytes, 0) == 0 || sbytes > 0) { sent = (ssize_t)sbytes; }
  else if (errno == ENOTSOCK) {
    sent = _sppc_send_by_copy(to_fd, from_fd, start, count);
    if (sent <= 0) { return sent; }
  }
  else { return -1; }

  if (offset != NULL) { *offset = (int64_t)(start + sent); }
  else if (lseek(from_fd, start + sent, SEEK_SET) < 0) { return -1; }
  return sent;
}

_posix_syscall(40)
_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_sendfile(const int from_fd, const int to_fd, int64_t *offset, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) _sppc_sendfile(to_fd, from_fd, offset, count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

// ==================== PROCESS HARDENING ====================

// FreeBSD's W^X switch for a process, "PROC_WXMAP_CTL", takes
// effect from its next exec, not for the image already
// running, and it has no mseal. Each fails the start-up, as on
// a Linux too old for it, naming the key that turns it off,
// rather than reporting a protection the process does not have.

_gnu_inline _gnu_cold
_sppc_api int sppc_deny_wx(void) {
  fprintf(stderr,
    "[sppc] could not deny writable-and-executable memory: FreeBSD has no switch a process can set for its own image\n"
    "  ('sysctl kern.elf64.allow_wx=0' enforces it for every program).\n"
    "  To run without it, set 'deny_wx = false' under [memory.map] in spp.toml and rebuild.\n");
  return SPPC_ENOSYS;
}

_gnu_inline _gnu_cold
_sppc_api int sppc_seal_segments(void) {
  fprintf(stderr,
    "[sppc] could not seal the program's memory mappings: FreeBSD has no mseal\n"
    "  To run without it, set 'seal = false' under [memory.map] in spp.toml and rebuild.\n");
  return SPPC_ENOSYS;
}
